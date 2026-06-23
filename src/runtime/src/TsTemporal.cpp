// Temporal API (TC39) runtime implementation. Self-contained library.
// See include/TsTemporal.h. The Temporal namespace + per-type constructors and
// prototypes are registered in TsGlobals.cpp (ts_get_global_Temporal); this
// file holds the per-type C++ instance classes and their value logic.
#include "TsTemporal.h"
#include "TsString.h"
#include "TsNanBox.h"
#include "TsRuntime.h"
#include "TsError.h"
#include "GC.h"
#include <new>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>

extern "C" double ts_to_number(TsValue* v);  // Primitives.cpp (throws on Symbol)

// Forward declarations for arithmetic/rounding helpers defined later in the file
// (so earlier method natives can call them).
static TsValue* time_diff_with_opts(long long diff, TsValue* opts, const char* defLargest);
static std::string read_string_option(TsValue* opts, const char* key, const char* def);
static bool option_to_string(TsValue* v, std::string* out);
static bool option_is_object(TsValue* v);
static bool temporal_mode_valid(const std::string& m);
static std::string read_enum_option(TsValue* opts, const char* key, const char* def, const char* const* valid, int nvalid);
static int iso_days_in_month(int y, int m);
static bool parse_round_options(TsValue* roundTo, std::string* unit, long long* inc, std::string* mode, int minRank, int maxRank);
static bool parse_timezone(const char* s, int* offMin, bool* isUtc);
static bool parse_iso_datetime(const char* s,int* Y,int* M,int* D,int* H,int* Mi,int* S,int* ms,int* us,int* ns);
static struct TsPlainDate* coerce_plaindate_arg(TsValue* v);
static void zdt_local(TsZonedDateTime* z,int* Y,int* M,int* D,int* h,int* mi,int* s,int* ms,int* us,int* ns);
static void iso_civil_from_days(long long z, int* y, int* m, int* d);
static long long unit_ns(const std::string& u, bool* ok);
// A `with`/partial bag must be a plain object — reject any Temporal-typed object.
// Magics at offset +16: PlainTime/Duration/PlainDate/PlainYearMonth/PlainMonthDay/
// PlainDateTime/Instant/ZonedDateTime.
static inline bool is_temporal_typed_object(void* raw){
    if(!raw || (uintptr_t)raw<4096 || (uintptr_t)raw>0x00007FFFFFFFFFFFULL) return false;
    uint32_t m=*(uint32_t*)((char*)raw+16);
    return m==0x504C5449||m==0x54445552||m==0x504C4454||m==0x504C594D||m==0x504C4D44||m==0x50444D54||m==0x494E5354||m==0x5A44544D;
}
// ToNumber on a BigInt or Symbol is a TypeError; reject before coercing an option
// value (e.g. roundingIncrement) without changing global ts_to_number.
static inline void reject_nonnumeric_increment(TsValue* v){
    if(!v) return;
    uint64_t nb=nanbox_from_tsvalue_ptr(v);
    if(!nanbox_is_ptr(nb)) return;
    void* r=nanbox_to_ptr(nb); if(!r) return;
    uint32_t m0=*(uint32_t*)r, m16=*(uint32_t*)((char*)r+16);
    if(m0==0x42494749||m16==0x42494749) ts_throw((TsValue*)ts_error_create_typed("TypeError","Cannot convert a BigInt value to a number"));
    if(m0==0x53594D42||m16==0x53594D42) ts_throw((TsValue*)ts_error_create_typed("TypeError","Cannot convert a Symbol value to a number"));
}
// ValidateTemporalRoundingIncrement (inclusive=false) for the TIME units of a
// difference: the increment must be < the unit's max (hour 24, minute/second 60,
// sub-second 1000) and divide it evenly. Calendar units (day+) are unconstrained.
static void validate_diff_time_increment(const std::string& smallest, long long inc){
    long long maxInc=0;
    if(smallest=="hour"||smallest=="hours") maxInc=24;
    else if(smallest=="minute"||smallest=="minutes"||smallest=="second"||smallest=="seconds") maxInc=60;
    else if(smallest=="millisecond"||smallest=="milliseconds"||smallest=="microsecond"||smallest=="microseconds"||smallest=="nanosecond"||smallest=="nanoseconds") maxInc=1000;
    if(maxInc>0){ long long i=inc>0?inc:1; if(i>=maxInc || maxInc%i!=0){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: roundingIncrement out of range")); } }
}
static long long round_nonneg(long long v, long long q, const std::string& mode);
static long long round_signed(long long v, long long q, const std::string& mode);
static std::string read_opt_str_noauto(void* raw, const char* key, const char* def);
static bool iso_annotations_valid(const char* s);
static int date_unit_rank(const std::string& u);
static long long unit_ns(const std::string& u, bool* ok);
static long long round_nonneg(long long v, long long q, const std::string& mode);
// ceil/floor/halfCeil/halfFloor are not symmetric about zero. round_nonneg rounds a
// non-negative magnitude; when the true value is negative and we re-apply the sign,
// the directional modes must be swapped so e.g. ceil(-x) rounds toward +inf, not -inf.
// trunc/expand/halfExpand/halfTrunc/halfEven are symmetric and stay unchanged.
static inline std::string flip_mode_neg(const std::string& mode){
    if(mode=="ceil") return "floor";
    if(mode=="floor") return "ceil";
    if(mode=="halfCeil") return "halfFloor";
    if(mode=="halfFloor") return "halfCeil";
    return mode;
}
// Round a true signed value v (any sign) under roundingMode `mode`, quantum q>0.
static inline long long round_signed(long long v, long long q, const std::string& mode){
    if(v<0) return -round_nonneg(-v, q, flip_mode_neg(mode));
    return round_nonneg(v, q, mode);
}
// rangeErr (out): set to true instead of throwing when the rounded upper-candidate date
// is outside the ISO range. The CALLER throws — round_date_duration holds a std::string
// local (rmode), and a longjmp (ts_throw) unwinding through this frame corrupts the MSVC
// unwinder (crash in basic_string::_Tidy_deallocate). See [[longjmp-stdstring-frame-crash]].
static void round_date_duration(int aY,int aM,int aD,int bY,int bM,int bD,
    const std::string& smallest,const std::string& largest,long long inc,const std::string& mode,
    long long* oy,long long* omo,long long* owk,long long* ody,bool* rangeErr=nullptr);
// Validate the shared rounding/diff option bag (roundingMode/smallestUnit/
// largestUnit/roundingIncrement), throwing TypeError/RangeError per spec.
// No-op when opts is undefined. Defined after read_string_option.
static void validate_round_diff_opts(TsValue* opts, int minRank, int maxRank);
static bool validate_overflow_option(TsValue* opts);
static void require_options_object(TsValue* opts);

TsPlainTime* TsPlainTime::Create(int h, int m, int s, int ms, int us, int ns) {
    void* mem = ts_alloc(sizeof(TsPlainTime));
    TsPlainTime* pt = new (mem) TsPlainTime();
    pt->magic = MAGIC;
    pt->iso_hour = h;
    pt->iso_minute = m;
    pt->iso_second = s;
    pt->iso_millisecond = ms;
    pt->iso_microsecond = us;
    pt->iso_nanosecond = ns;
    return pt;
}

TsValue TsPlainTime::GetPropertyVirtual(const char* key) {
    auto mk = [](int v) { TsValue r; r.type = ValueType::NUMBER_INT; r.i_val = v; return r; };
    if (strcmp(key, "hour") == 0) return mk(iso_hour);
    if (strcmp(key, "minute") == 0) return mk(iso_minute);
    if (strcmp(key, "second") == 0) return mk(iso_second);
    if (strcmp(key, "millisecond") == 0) return mk(iso_millisecond);
    if (strcmp(key, "microsecond") == 0) return mk(iso_microsecond);
    if (strcmp(key, "nanosecond") == 0) return mk(iso_nanosecond);
    TsValue u; u.type = ValueType::UNDEFINED; u.i_val = 0; return u;
}

extern "C" TsValue* ts_temporal_plaintime_construct(int argc, TsValue** argv) {
    // ECMA-262 Temporal.PlainTime(hour, minute, second, ms, us, ns): each field
    // is undefined -> 0, else ToIntegerWithTruncation (ToNumber throws TypeError
    // on a Symbol; non-finite -> RangeError). Then RejectTime range-check.
    auto field = [&](int i) -> int {
        if (i >= argc || !argv || !argv[i] || ts_value_is_undefined(argv[i])) return 0;
        double d = ts_to_number(argv[i]);  // may throw TypeError (Symbol/BigInt)
        if (d != d || std::isinf(d)) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "Temporal.PlainTime field must be a finite number"));
            return 0;  // unreachable
        }
        return (int)std::trunc(d);
    };
    int h = field(0), m = field(1), s = field(2);
    int ms = field(3), us = field(4), ns = field(5);
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 ||
        ms < 0 || ms > 999 || us < 0 || us > 999 || ns < 0 || ns > 999) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "Temporal.PlainTime field out of range"));
        return ts_value_make_undefined();  // unreachable
    }
    return ts_value_make_object(TsPlainTime::Create(h, m, s, ms, us, ns));
}

// ---- prototype + static method natives ----
extern "C" {
    void* ts_get_call_this();
    void* ts_nanbox_safe_unbox(void* v);
    TsValue* ts_object_get_property(void* obj, const char* key);
    void* ts_temporal_get_plaintime_ctor();
    TsValue* ts_temporal_plaintime_from(int argc, TsValue** argv);
}

// Safe PlainTime brand-check by magic. A receiver may be a TsString (or other
// non-TsObject); dynamic_cast<TsPlainTime*>((TsObject*)str) is UB and crashes,
// so bail on the string magic first, then check the offset-16 type tag.
static TsPlainTime* as_plaintime(void* raw) {
    if (!raw) return nullptr;
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 || m0 == 0x434F4E53) return nullptr;  // TsString / TsConsString
    return (*(uint32_t*)((char*)raw + 16) == TsPlainTime::MAGIC) ? (TsPlainTime*)raw : nullptr;
}

// thisTemporalTime(this): brand-check or TypeError.
static TsPlainTime* require_plaintime(void* ctx, const char* method) {
    if (!ctx) ctx = ts_get_call_this();
    void* raw = ts_nanbox_safe_unbox(ctx);
    TsPlainTime* pt = as_plaintime(raw);
    if (!pt) {
        std::string msg = std::string("Temporal.PlainTime.prototype.") + method +
            " called on an object that is not a Temporal.PlainTime";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
    }
    return pt;
}

// ECMA-262 TemporalTimeToString with the "auto" fractional-second precision:
// HH:MM:SS and, if any sub-second component is non-zero, a trimmed fraction.
static TsString* plaintime_iso_string(TsPlainTime* pt) {
    char buf[32];
    long frac = (long)pt->iso_millisecond * 1000000L +
                (long)pt->iso_microsecond * 1000L + (long)pt->iso_nanosecond;
    int n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     pt->iso_hour, pt->iso_minute, pt->iso_second);
    if (frac > 0) {
        char fb[16];
        snprintf(fb, sizeof(fb), "%09ld", frac);
        int len = 9;
        while (len > 1 && fb[len-1] == '0') len--;
        buf[n++] = '.';
        for (int i = 0; i < len; i++) buf[n++] = fb[i];
        buf[n] = '\0';
    }
    return TsString::Create(buf);
}

extern "C" {

// Format HH:MM[:SS[.frac]] honoring smallestUnit / fractionalSecondDigits /
// roundingMode (default trunc). Returns the time-of-day clamped mod 24h (carry
// is dropped — fine for PlainTime; PlainDateTime trunc default never carries).
static std::string format_time_opts(int h,int mi,int s,int ms,int us,int ns, TsValue* opts, int* dayCarry=nullptr){
    if(dayCarry) *dayCarry=0;
    std::string smallest = read_string_option(opts,"smallestUnit","");
    // roundingMode read WITHOUT the "auto"->default mapping: "auto" is not a valid
    // roundingMode and must be rejected (read_string_option would swallow it).
    void* rmraw = opts?ts_nanbox_safe_unbox(opts):nullptr;
    std::string mode = read_opt_str_noauto(rmraw,"roundingMode","trunc");
    // Validate the time-rounding options (RangeError on an invalid value).
    if(!smallest.empty()){
        bool ok = smallest=="minute"||smallest=="minutes"||smallest=="second"||smallest=="seconds"
               || smallest=="millisecond"||smallest=="milliseconds"||smallest=="microsecond"||smallest=="microseconds"
               || smallest=="nanosecond"||smallest=="nanoseconds";
        if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid smallestUnit")); }
    }
    if(!temporal_mode_valid(mode)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingMode")); }
    int fsd=-1; // -1 = auto
    void* raw = opts?ts_nanbox_safe_unbox(opts):nullptr;
    if(raw){ TsValue* f=ts_object_get_property(raw,"fractionalSecondDigits");
        if(f&&!ts_value_is_undefined(f)){
            // GetStringOrNumberOption: a Number is range-checked to [0,9] (floored);
            // a NON-Number is valid ONLY if it is the string "auto" — anything else
            // (null/true/false/object/non-"auto" string) is a RangeError. (Previously
            // we ToNumber'd non-Numbers, so null->0 etc. slipped through.)
            uint64_t fnb=nanbox_from_tsvalue_ptr(f);
            if(nanbox_is_number(fnb)){
                double dv=ts_to_number(f);
                if(!(dv==dv) || std::isinf(dv)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fractionalSecondDigits must be \"auto\" or an integer 0-9")); }
                long long fv=(long long)std::floor(dv);   // floor first, then range-check (9.7 -> 9 is valid)
                if(fv<0 || fv>9){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fractionalSecondDigits must be \"auto\" or an integer 0-9")); }
                fsd=(int)fv;
            } else {
                // Non-Number: ToString (invokes a toString/valueOf observer) and
                // require "auto"; anything else is a RangeError.
                std::string fs;
                if(!option_to_string(f,&fs) || fs!="auto"){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fractionalSecondDigits must be \"auto\" or an integer 0-9")); }
            } } }
    long long tns = ((long long)h*3600+(long long)mi*60+s)*1000000000LL + (long long)ms*1000000 + (long long)us*1000 + ns;
    int digits=-1; bool dropSeconds=false; long long unitNs=1;
    if(smallest=="minute"||smallest=="minutes"){ dropSeconds=true; digits=0; unitNs=60000000000LL; }
    else if(smallest=="second"||smallest=="seconds"){ digits=0; unitNs=1000000000LL; }
    else if(smallest=="millisecond"||smallest=="milliseconds"){ digits=3; unitNs=1000000LL; }
    else if(smallest=="microsecond"||smallest=="microseconds"){ digits=6; unitNs=1000LL; }
    else if(smallest=="nanosecond"||smallest=="nanoseconds"){ digits=9; unitNs=1LL; }
    else if(fsd>=0){ digits=fsd; unitNs=1; for(int i=0;i<9-fsd;i++)unitNs*=10; }
    if(unitNs>1){
        long long q=unitNs,v=tns,quo=v/q,r=v%q,rounded;
        if(r==0) rounded=v;
        else if(mode=="trunc"||mode=="floor") rounded=quo*q;
        else if(mode=="ceil"||mode=="expand") rounded=(quo+1)*q;
        else if(mode=="halfExpand"||mode=="halfCeil") rounded=(r*2>=q)?(quo+1)*q:quo*q;
        else if(mode=="halfTrunc"||mode=="halfFloor") rounded=(r*2>q)?(quo+1)*q:quo*q;
        else if(mode=="halfEven"){ if(r*2>q)rounded=(quo+1)*q; else if(r*2<q)rounded=quo*q; else rounded=(quo%2==0)?quo*q:(quo+1)*q; }
        else rounded=quo*q;
        long long carry=rounded/86400000000000LL; tns=rounded % 86400000000000LL; if(tns<0){ tns+=86400000000000LL; carry--; }
        if(dayCarry) *dayCarry=(int)carry;   // rounding crossed midnight -> the caller advances the date
    }
    int H=(int)(tns/3600000000000LL); tns%=3600000000000LL;
    int M=(int)(tns/60000000000LL); tns%=60000000000LL;
    int S=(int)(tns/1000000000LL); long long frac=tns%1000000000LL;
    char hb[24];
    if(dropSeconds){ snprintf(hb,sizeof(hb),"%02d:%02d",H,M); return hb; }
    snprintf(hb,sizeof(hb),"%02d:%02d:%02d",H,M,S); std::string out=hb;
    if(digits>0){ char fb[16]; snprintf(fb,sizeof(fb),"%09lld",frac); out += "."+std::string(fb).substr(0,digits); }
    else if(digits<0 && frac>0){ char fb[16]; snprintf(fb,sizeof(fb),"%09lld",frac); std::string f(fb); while(!f.empty()&&f.back()=='0')f.pop_back(); out+="."+f; }
    return out;
}

TsValue* ts_temporal_plaintime_toString_native(void* ctx, int argc, TsValue** argv) {
    TsPlainTime* pt = require_plaintime(ctx, "toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    TsValue* opts=(argc>=1&&argv)?argv[0]:nullptr;
    if(!opts||ts_value_is_undefined(opts)) return ts_value_make_string(plaintime_iso_string(pt));
    return ts_value_make_string(TsString::Create(
        format_time_opts(pt->iso_hour,pt->iso_minute,pt->iso_second,pt->iso_millisecond,pt->iso_microsecond,pt->iso_nanosecond,opts).c_str()));
}

TsValue* ts_temporal_plaintime_valueOf_native(void* ctx, int argc, TsValue** argv) {
    // Temporal types are not comparable with </> ; valueOf always throws to
    // surface accidental arithmetic/relational use (ECMA-262 Temporal).
    (void)ctx;
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "Called valueOf on a Temporal.PlainTime; use compare() or equals() instead"));
    return ts_value_make_undefined();
}

// Extract a std::string from a TsValue iff it is actually a string.
static bool tsvalue_to_stdstring(TsValue* v, std::string* out) {
    void* raw = v ? ts_nanbox_safe_unbox(v) : nullptr;
    if (!raw) return false;
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 != 0x53545247 && m0 != 0x434F4E53) return false;
    void* sp = ts_value_get_string(v);
    if (!sp) return false;
    // Length-aware extraction so an embedded NUL (e.g. an option value
    // "millisecond\0") survives; a const char* + std::string assignment would
    // re-truncate at the NUL even though the TsString stored all the bytes.
    out->clear();
    ((TsString*)sp)->AppendUtf8(*out);
    return true;
}

// Temporal.PlainTime.prototype.round(roundTo) — round the wall-clock time to a
// smallestUnit (hour..nanosecond) with a roundingIncrement and roundingMode.
TsValue* ts_temporal_plaintime_round_native(void* ctx, int argc, TsValue** argv) {
    TsPlainTime* pt = require_plaintime(ctx, "round");
    TsValue* roundTo = (argc >= 1 && argv) ? argv[0] : nullptr;
    if (!roundTo || ts_value_is_undefined(roundTo)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainTime.prototype.round: roundTo is required"));
        return ts_value_make_undefined();
    }
    std::string unit, mode = "halfExpand";
    long increment = 1;
    if (!tsvalue_to_stdstring(roundTo, &unit)) {
        // options bag: smallestUnit (required), roundingIncrement, roundingMode.
        void* raw = ts_nanbox_safe_unbox(roundTo);
        if (!raw) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Temporal.PlainTime.prototype.round: roundTo must be a string or object"));
            return ts_value_make_undefined();
        }
        TsValue* su = ts_object_get_property(raw, "smallestUnit");
        if (!su || ts_value_is_undefined(su) || !tsvalue_to_stdstring(su, &unit)) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "Temporal.PlainTime.prototype.round: smallestUnit is required"));
            return ts_value_make_undefined();
        }
        TsValue* ri = ts_object_get_property(raw, "roundingIncrement");
        if (ri && !ts_value_is_undefined(ri)) {
            double d = (reject_nonnumeric_increment(ri), ts_to_number(ri));   // ToNumber: null->0, undefined skipped above
            if (!(d == d) || std::isinf(d)) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainTime.prototype.round: invalid roundingIncrement")); return ts_value_make_undefined(); }
            increment = (long)std::trunc(d);
            if (increment < 1) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainTime.prototype.round: roundingIncrement out of range")); return ts_value_make_undefined(); }
        }
        TsValue* rm = ts_object_get_property(raw, "roundingMode");
        std::string m; if (rm && option_to_string(rm, &m)) { if(!temporal_mode_valid(m)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainTime.prototype.round: invalid roundingMode")); return ts_value_make_undefined(); } mode = m; }
    }
    long long unitNs;
    if (unit == "hour" || unit == "hours") unitNs = 3600000000000LL;
    else if (unit == "minute" || unit == "minutes") unitNs = 60000000000LL;
    else if (unit == "second" || unit == "seconds") unitNs = 1000000000LL;
    else if (unit == "millisecond" || unit == "milliseconds") unitNs = 1000000LL;
    else if (unit == "microsecond" || unit == "microseconds") unitNs = 1000LL;
    else if (unit == "nanosecond" || unit == "nanoseconds") unitNs = 1LL;
    else {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "Temporal.PlainTime.prototype.round: invalid smallestUnit"));
        return ts_value_make_undefined();
    }
    if (increment < 1) increment = 1;
    validate_diff_time_increment(unit, increment);
    long long quantum = unitNs * (long long)increment;
    long long nsOfDay = ((((long long)pt->iso_hour * 60 + pt->iso_minute) * 60 + pt->iso_second) * 1000000000LL)
        + (long long)pt->iso_millisecond * 1000000LL + (long long)pt->iso_microsecond * 1000LL + pt->iso_nanosecond;
    long long q = nsOfDay / quantum, r = nsOfDay % quantum;
    long long rounded;
    if (mode == "ceil" || mode == "expand") rounded = (r > 0) ? (q + 1) * quantum : nsOfDay;
    else if (mode == "floor" || mode == "trunc") rounded = q * quantum;
    else if (mode == "halfEven") {
        if (r * 2 > quantum) rounded = (q + 1) * quantum;
        else if (r * 2 < quantum) rounded = q * quantum;
        else rounded = (q % 2 == 0) ? q * quantum : (q + 1) * quantum;
    } else if (mode == "halfFloor" || mode == "halfTrunc")
        rounded = (r * 2 > quantum) ? (q + 1) * quantum : q * quantum;
    else  // halfExpand (default), halfCeil
        rounded = (r * 2 >= quantum) ? (q + 1) * quantum : q * quantum;
    rounded %= 86400000000000LL;  // wrap within a 24h day
    int h  = (int)(rounded / 3600000000000LL); rounded %= 3600000000000LL;
    int m  = (int)(rounded / 60000000000LL);   rounded %= 60000000000LL;
    int s  = (int)(rounded / 1000000000LL);    rounded %= 1000000000LL;
    int ms = (int)(rounded / 1000000LL);       rounded %= 1000000LL;
    int us = (int)(rounded / 1000LL);          int ns = (int)(rounded % 1000LL);
    return ts_value_make_object(TsPlainTime::Create(h, m, s, ms, us, ns));
}

// Temporal.PlainTime.prototype.with(timeLike, options?) — returns a new
// PlainTime with the provided fields overridden (others kept). The argument
// must be a plain object with >=1 recognized field; a Temporal type or a
// primitive throws TypeError. Default overflow "constrain" clamps.
TsValue* ts_temporal_plaintime_with_native(void* ctx, int argc, TsValue** argv) {
    TsPlainTime* pt = require_plaintime(ctx, "with");
    bool _ovrej = validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr);
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* raw = arg ? ts_nanbox_safe_unbox(arg) : nullptr;
    if (!raw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainTime.prototype.with: argument must be an object"));
        return ts_value_make_undefined();
    }
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 || m0 == 0x434F4E53 ||
        is_temporal_typed_object(raw)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainTime.prototype.with: argument must be a plain object"));
        return ts_value_make_undefined();
    }
    static const char* names[6] = {"hour","minute","second","millisecond","microsecond","nanosecond"};
    const int lim[6] = {23,59,59,999,999,999};
    int vals[6] = {pt->iso_hour,pt->iso_minute,pt->iso_second,
                   pt->iso_millisecond,pt->iso_microsecond,pt->iso_nanosecond};
    bool any = false;
    for (int i = 0; i < 6; i++) {
        TsValue* f = ts_object_get_property(raw, names[i]);
        if (f && !ts_value_is_undefined(f)) {
            any = true;
            double d = ts_to_number(f);
            if (d != d || std::isinf(d)) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError",
                    "Temporal.PlainTime.prototype.with: field is not finite"));
                return ts_value_make_undefined();
            }
            int v = (int)std::trunc(d);
            if (_ovrej && (v < 0 || v > lim[i])) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainTime.prototype.with: field out of range (overflow reject)")); return ts_value_make_undefined(); }
            if (v < 0) v = 0; if (v > lim[i]) v = lim[i];
            vals[i] = v;
        }
    }
    if (!any) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainTime.prototype.with: object has no recognized time fields"));
        return ts_value_make_undefined();
    }
    return ts_value_make_object(TsPlainTime::Create(vals[0],vals[1],vals[2],vals[3],vals[4],vals[5]));
}

TsValue* ts_temporal_plaintime_equals_native(void* ctx, int argc, TsValue** argv) {
    TsPlainTime* a = require_plaintime(ctx, "equals");
    // ToTemporalTime(other) then compare ISO fields.
    TsValue* other = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* raw = other ? ts_nanbox_safe_unbox(other) : nullptr;
    TsPlainTime* b = as_plaintime(raw);
    if (!b) {
        // Coerce via from() for objects/strings.
        TsValue* coerced = ts_temporal_plaintime_from(other ? 1 : 0, &other);
        void* craw = ts_nanbox_safe_unbox(coerced);
        b = as_plaintime(craw);
        if (!b) return ts_value_make_bool(false);
    }
    bool eq = a->iso_hour==b->iso_hour && a->iso_minute==b->iso_minute &&
              a->iso_second==b->iso_second && a->iso_millisecond==b->iso_millisecond &&
              a->iso_microsecond==b->iso_microsecond && a->iso_nanosecond==b->iso_nanosecond;
    return ts_value_make_bool(eq);
}

// Static Temporal.PlainTime.compare(a, b) -> -1 | 0 | 1
TsValue* ts_temporal_plaintime_compare_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    auto toPT = [](TsValue* v) -> TsPlainTime* {
        void* raw = v ? ts_nanbox_safe_unbox(v) : nullptr;
        TsPlainTime* pt = as_plaintime(raw);
        if (pt) return pt;
        TsValue* c = ts_temporal_plaintime_from(v ? 1 : 0, &v);
        void* craw = ts_nanbox_safe_unbox(c);
        return as_plaintime(craw);
    };
    TsPlainTime* a = toPT((argc >= 1) ? argv[0] : nullptr);
    TsPlainTime* b = toPT((argc >= 2) ? argv[1] : nullptr);
    if (!a || !b) return ts_value_make_int(0);
    int af[6] = {a->iso_hour,a->iso_minute,a->iso_second,a->iso_millisecond,a->iso_microsecond,a->iso_nanosecond};
    int bf[6] = {b->iso_hour,b->iso_minute,b->iso_second,b->iso_millisecond,b->iso_microsecond,b->iso_nanosecond};
    for (int i = 0; i < 6; i++) {
        if (af[i] < bf[i]) return ts_value_make_int(-1);
        if (af[i] > bf[i]) return ts_value_make_int(1);
    }
    return ts_value_make_int(0);
}

// Static Temporal.PlainTime.from(item, options?). Handles a PlainTime (clone),
// a property bag (hour/minute/... fields), or an ISO time string.
TsValue* ts_temporal_plaintime_from_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    return ts_temporal_plaintime_from(argc, argv);
}

} // extern "C"

// ISO 8601 only permits the ASCII minus '-' (U+002D) as a sign. The Unicode
// minus U+2212 (UTF-8 E2 88 92) is never valid, so a string containing it is
// rejected outright (used for year and UTC-offset signs in test262).
static inline bool has_unicode_minus(const char* s){
    return s && strstr(s, "\xe2\x88\x92") != nullptr;
}
// A wall-clock type (PlainDate/Time/DateTime/YearMonth/MonthDay) must reject a
// string carrying the UTC designator 'Z'/'z' (that denotes an exact instant).
// The designator appears in the datetime portion, before any '[' annotation.
static inline bool has_utc_designator(const char* s){
    if(!s) return false;
    for(const char* p=s; *p && *p!='['; p++){ if(*p=='Z'||*p=='z') return true; }
    return false;
}
// A signed year of zero ("-000000") is not a valid extended year. Detect a
// leading ASCII '-' followed by one or more digits that are all zero.
static inline bool has_negative_zero_year(const char* s){
    if(!s || *s!='-') return false;
    const char* d=s+1; const char* p=d;
    while(*p>='0'&&*p<='9') p++;
    if(p==d) return false;
    for(const char* q=d; q<p; q++) if(*q!='0') return false;
    return true;
}
// Parse an ISO-8601 time (optionally preceded by a date + 'T'). Fills the six
// fields. Returns false on malformed input or a UTC 'Z' designator (a bare
// PlainTime string must not carry a zone). Lenient on separators.
// A bare (no-'T') string that is itself a valid calendar date — a full date
// "YYYY-MM-DD", or one of the reduced forms that ALSO reads as a valid date
// ("YYYY-MM"/"YYYYMM" with a real month, "MMDD"/"MM-DD" with a real month+day) —
// is ambiguous for a PlainTime and must be rejected (spec ParseTemporalTimeString
// requires a 'T' to disambiguate). A reduced form whose month/day is out of range
// (e.g. "2021-13", "1232", "0230") is NOT a date, so it parses as a time.
static bool ambiguous_date_for_time(const char* s) {
    int n = 0; while (s[n] && s[n] != '[') n++;   // length before any annotation
    auto alld = [&](int a,int b){ for(int i=a;i<b;i++) if(!isdigit((unsigned char)s[i])) return false; return true; };
    auto d2 = [&](int i){ return (s[i]-'0')*10 + (s[i+1]-'0'); };
    auto monOk = [](int M){ return M>=1 && M<=12; };
    auto dayOk = [](int M,int D){ return M>=1 && M<=12 && D>=1 && D<=iso_days_in_month(2000,M); }; // 2000 leap -> 0229 counts
    if (n==4  && alld(0,4))                                   return dayOk(d2(0), d2(2));          // MMDD
    if (n==5  && alld(0,2) && s[2]=='-' && alld(3,5))         return dayOk(d2(0), d2(3));          // MM-DD
    if (n==6  && alld(0,6))                                   return monOk(d2(4));                 // YYYYMM
    if (n==7  && alld(0,4) && s[4]=='-' && alld(5,7))         return monOk(d2(5));                 // YYYY-MM
    if (n>=10 && alld(0,4) && s[4]=='-' && alld(5,7) && s[7]=='-' && alld(8,10)) {                 // YYYY-MM-DD
        return monOk(d2(5)) && d2(8)>=1 && d2(8)<=iso_days_in_month(2000,d2(5));
    }
    return false;
}
static bool parse_iso_time(const char* s, int* H, int* M, int* S,
                           int* ms, int* us, int* ns) {
    if (has_unicode_minus(s) || has_negative_zero_year(s)) return false;
    const char* t = s;
    // Only the date/time 'T' separator counts — stop at '[' so a 'T' inside a
    // time-zone annotation like "[UTC]"/"[America/St_Johns]" is not mistaken
    // for the separator (which would make us parse time from mid-annotation).
    // 'T'/'t' is a separator OR a bare-time prefix; a space is a separator ONLY when
    // it follows a date (a digit) — a leading space (" 2021-12") is not a T substitute.
    for (const char* p = s; *p && *p != '['; p++) { if (*p == 'T' || *p == 't' || (*p == ' ' && p > s && isdigit((unsigned char)p[-1]))) { t = p + 1; break; } }
    // Without a 'T', reject a string that is itself a valid (reduced) calendar
    // date — ambiguous for a PlainTime (no implicit midnight / time designator).
    if (t == s && ambiguous_date_for_time(s)) return false;
    auto two = [](const char* p, int* out) -> const char* {
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return nullptr;
        *out = (p[0]-'0')*10 + (p[1]-'0'); return p + 2;
    };
    int h = 0, m = 0, sec = 0; long frac = 0;
    const char* p = two(t, &h); if (!p) return false;
    if (*p == ':') p++;
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
        p = two(p, &m);
        if (*p == ':') p++;
        if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1])) {
            p = two(p, &sec);
            if (*p == '.' || *p == ',') {
                p++;
                char fb[10] = "000000000"; int i = 0;
                while (i < 9 && isdigit((unsigned char)*p)) { fb[i++] = *p++; }
                while (isdigit((unsigned char)*p)) p++;  // excess digits ignored
                frac = atol(fb);
            }
        }
    }
    if (*p == 'Z' || *p == 'z') return false;  // UTC designator invalid for PlainTime
    if(sec==60) sec=59;   // leap second -> constrain to :59 (Temporal has no leap seconds)
    *H = h; *M = m; *S = sec;
    *ms = (int)(frac / 1000000); *us = (int)((frac / 1000) % 1000); *ns = (int)(frac % 1000);
    return true;
}

extern "C" TsValue* ts_temporal_plaintime_from(int argc, TsValue** argv) {
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    TsValue* item = (argc >= 1 && argv) ? argv[0] : nullptr;
    if (!item || ts_value_is_undefined(item)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainTime.from: argument is undefined"));
        return ts_value_make_undefined();
    }
    void* raw = ts_nanbox_safe_unbox(item);
    // Existing Temporal.PlainTime -> clone its fields.
    if (raw) {
        uint32_t m16 = *(uint32_t*)((char*)raw + 16);
        if (m16 == TsPlainTime::MAGIC) {
            TsPlainTime* o = (TsPlainTime*)raw;
            validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr);   // validated even for a PlainTime arg
            return ts_value_make_object(TsPlainTime::Create(
                o->iso_hour, o->iso_minute, o->iso_second,
                o->iso_millisecond, o->iso_microsecond, o->iso_nanosecond));
        }
    }
    // ISO time string. Gate on the real string magic — ts_value_get_string
    // coerces non-strings (an object would otherwise parse "[object Object]").
    if (raw && (*(uint32_t*)raw == 0x53545247 /*STRG*/ || *(uint32_t*)raw == 0x434F4E53 /*CONS*/)) {
        void* strPtr = ts_value_get_string(item);
        const char* utf = strPtr ? ((TsString*)strPtr)->ToUtf8() : nullptr;
        int H, M, S, ms, us, ns;
        if (!utf || !parse_iso_time(utf, &H, &M, &S, &ms, &us, &ns) ||
            H < 0 || H > 23 || M < 0 || M > 59 || S < 0 || S > 59 || !iso_annotations_valid(utf)) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "Temporal.PlainTime.from: string is not a valid ISO time"));
            return ts_value_make_undefined();
        }
        validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr);   // read AFTER a valid parse
        return ts_value_make_object(TsPlainTime::Create(H, M, S, ms, us, ns));
    }
    // Property bag: read recognized fields (default overflow "constrain" clamps).
    if (raw) {
        static const char* names[6] = {"hour","minute","second","millisecond","microsecond","nanosecond"};
        int vals[6] = {0,0,0,0,0,0};
        const int lim[6] = {23,59,59,999,999,999};
        bool any = false; int rawv[6] = {0,0,0,0,0,0};
        for (int i = 0; i < 6; i++) {
            TsValue* f = ts_object_get_property(raw, names[i]);
            if (f && !ts_value_is_undefined(f)) {
                any = true;
                double d = ts_to_number(f);
                if (d != d || std::isinf(d)) {
                    ts_throw((TsValue*)ts_error_create_typed("RangeError",
                        "Temporal.PlainTime.from: field is not finite"));
                    return ts_value_make_undefined();
                }
                rawv[i] = (int)std::trunc(d);
            }
        }
        if (!any) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Temporal.PlainTime.from: object has no recognized time fields"));
            return ts_value_make_undefined();
        }
        // overflow is read AFTER the fields (observable order); reject rejects
        // out-of-range, otherwise constrain clamps.
        bool _ovrej = validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr);
        for (int i = 0; i < 6; i++) {
            int v = rawv[i];
            if (_ovrej && (v < 0 || v > lim[i])) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainTime.from: field out of range (overflow reject)")); return ts_value_make_undefined(); }
            if (v < 0) v = 0; if (v > lim[i]) v = lim[i];
            vals[i] = v;
        }
        return ts_value_make_object(TsPlainTime::Create(vals[0],vals[1],vals[2],vals[3],vals[4],vals[5]));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "Temporal.PlainTime.from: invalid argument"));
    return ts_value_make_undefined();
}

// ============================ Temporal.Duration ============================
TsDuration* TsDuration::Create(long long y, long long mo, long long w, long long d, long long h,
                               long long mi, long long s, long long ms, long long us, long long ns) {
    void* mem = ts_alloc(sizeof(TsDuration));
    TsDuration* du = new (mem) TsDuration();
    du->magic = MAGIC;
    du->years=y; du->months=mo; du->weeks=w; du->days=d; du->hours=h;
    du->minutes=mi; du->seconds=s; du->milliseconds=ms; du->microseconds=us; du->nanoseconds=ns;
    return du;
}

int TsDuration::Sign() const {
    long long f[10] = {years,months,weeks,days,hours,minutes,seconds,milliseconds,microseconds,nanoseconds};
    for (int i = 0; i < 10; i++) { if (f[i] > 0) return 1; if (f[i] < 0) return -1; }
    return 0;
}

TsValue TsDuration::GetPropertyVirtual(const char* key) {
    auto mk = [](long long v) { TsValue r; r.type = ValueType::NUMBER_INT; r.i_val = v; return r; };
    if (strcmp(key,"years")==0) return mk(years);
    if (strcmp(key,"months")==0) return mk(months);
    if (strcmp(key,"weeks")==0) return mk(weeks);
    if (strcmp(key,"days")==0) return mk(days);
    if (strcmp(key,"hours")==0) return mk(hours);
    if (strcmp(key,"minutes")==0) return mk(minutes);
    if (strcmp(key,"seconds")==0) return mk(seconds);
    if (strcmp(key,"milliseconds")==0) return mk(milliseconds);
    if (strcmp(key,"microseconds")==0) return mk(microseconds);
    if (strcmp(key,"nanoseconds")==0) return mk(nanoseconds);
    if (strcmp(key,"sign")==0) return mk(Sign());
    if (strcmp(key,"blank")==0) { TsValue r; r.type=ValueType::BOOLEAN; r.i_val=(Sign()==0); return r; }
    TsValue u; u.type = ValueType::UNDEFINED; u.i_val = 0; return u;
}

static TsDuration* as_duration(void* raw) {
    if (!raw) return nullptr;
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 || m0 == 0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw + 16) == TsDuration::MAGIC) ? (TsDuration*)raw : nullptr;
}

// ToIntegerIfIntegral: finite + integral, else RangeError.
static long long duration_field(TsValue* v, bool* ok) {
    if (!v || ts_value_is_undefined(v)) return 0;
    reject_nonnumeric_increment(v);   // ToNumber(BigInt) is a TypeError
    double d = ts_to_number(v);  // throws on Symbol
    if (d != d || std::isinf(d) || d != std::trunc(d)) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "Temporal.Duration: components must be integers"));
        *ok = false; return 0;
    }
    return (long long)d;
}

// All non-zero components must share one sign.
static bool duration_same_sign(long long* f) {
    int sign = 0;
    for (int i = 0; i < 10; i++) {
        if (f[i] == 0) continue;
        int s = f[i] > 0 ? 1 : -1;
        if (sign == 0) sign = s; else if (s != sign) return false;
    }
    return true;
}
// ECMAScript IsValidDuration range check: |years|,|months|,|weeks| < 2^32, and the
// total seconds (days*86400 + time) must not exceed 2^53 in magnitude. f = the 10
// components {y,mo,w,d,h,mi,s,ms,us,ns}.
static bool duration_in_range(long long* f) {
    for (int i = 0; i < 3; i++) if (f[i] >= 4294967296LL || f[i] <= -4294967296LL) return false;
    // The total nanoseconds must satisfy abs(ns) <= 2^53*1e9 - 1, i.e. the whole-
    // second total must satisfy abs(seconds) <= 2^53-1. Compute the integer second
    // count (with carry from the ms/us/ns components) exactly to avoid double
    // rounding at the boundary.
    long long carrySec = f[7]/1000 + f[8]/1000000 + f[9]/1000000000;
    long long remNs = (f[7]%1000)*1000000LL + (f[8]%1000000)*1000LL + (f[9]%1000000000);
    carrySec += remNs/1000000000LL;
    long long intSec = f[3]*86400LL + f[4]*3600LL + f[5]*60LL + f[6] + carrySec;
    long long absSec = intSec<0?-intSec:intSec;
    if (absSec > 9007199254740991LL) return false;
    return true;
}

extern "C" TsValue* ts_temporal_duration_construct(int argc, TsValue** argv) {
    long long f[10]; bool ok = true;
    for (int i = 0; i < 10; i++) {
        f[i] = duration_field((i < argc && argv) ? argv[i] : nullptr, &ok);
        if (!ok) return ts_value_make_undefined();
    }
    if (!duration_in_range(f)) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration: a component is out of range"));
        return ts_value_make_undefined();
    }
    if (!duration_same_sign(f)) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError",
            "Temporal.Duration: mixed-sign components are not allowed"));
        return ts_value_make_undefined();
    }
    return ts_value_make_object(TsDuration::Create(f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9]));
}

extern "C" {
    void* ts_temporal_get_duration_ctor();
    TsValue* ts_temporal_duration_from(int argc, TsValue** argv);
}

static TsDuration* require_duration(void* ctx, const char* method) {
    if (!ctx) ctx = ts_get_call_this();
    TsDuration* d = as_duration(ts_nanbox_safe_unbox(ctx));
    if (!d) {
        std::string msg = std::string("Temporal.Duration.prototype.") + method +
            " called on an object that is not a Temporal.Duration";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
    }
    return d;
}

// ISO-8601 duration string. Sign prefix; PnYnMnWnD then T nH nM nS (seconds
// carries a decimal fraction from ms/us/ns). All-zero -> "PT0S".
static TsString* duration_iso_string(TsDuration* d, TsValue* opts=nullptr) {
    int sign = d->Sign();
    std::string out;
    if (sign < 0) out += "-";
    out += "P";
    auto u = [](long long v) -> long long { return v < 0 ? -v : v; };
    char b[32];
    if (d->years)  { snprintf(b,sizeof(b),"%lldY",u(d->years));  out += b; }
    if (d->months) { snprintf(b,sizeof(b),"%lldM",u(d->months)); out += b; }
    if (d->weeks)  { snprintf(b,sizeof(b),"%lldW",u(d->weeks));  out += b; }
    if (d->days)   { snprintf(b,sizeof(b),"%lldD",u(d->days));   out += b; }
    long long sec = u(d->seconds);
    long long frac = u(d->milliseconds)*1000000LL + u(d->microseconds)*1000LL + u(d->nanoseconds);
    sec += frac/1000000000LL; frac %= 1000000000LL;   // carry whole seconds out of the sub-second total
    // toString options: round the sub-second to fractionalSecondDigits / smallestUnit
    // (roundingMode applied; a carry adds whole seconds). -1 = "auto" (trim zeros).
    int digits = -1;
    if (opts && !ts_value_is_undefined(opts)) {
        void* raw = ts_nanbox_safe_unbox(opts);
        // Validate smallestUnit / roundingMode only when PRESENT (a present-but-invalid
        // value, including null, is a RangeError; absent keeps the default).
        std::string smallest = "";
        std::string mode = "trunc";
        if(raw){
            TsValue* su=ts_object_get_property(raw,"smallestUnit");
            if(su && !ts_value_is_undefined(su)){
                static const char* DTSU[8]={"second","seconds","millisecond","milliseconds","microsecond","microseconds","nanosecond","nanoseconds"};
                std::string s2;   // option_to_string APPENDS — start from empty
                if(!option_to_string(su,&s2)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.toString: invalid smallestUnit")); }
                bool ok=false; for(int i=0;i<8;i++) if(s2==DTSU[i]) ok=true;
                if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.toString: invalid smallestUnit")); }
                smallest=s2;
            }
            TsValue* rm=ts_object_get_property(raw,"roundingMode");
            if(rm && !ts_value_is_undefined(rm)){
                std::string m2;
                if(!option_to_string(rm,&m2) || !temporal_mode_valid(m2)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.toString: invalid roundingMode")); }
                mode=m2;
            }
        }
        int fsd = -1;
        if (raw) { TsValue* f = ts_object_get_property(raw, "fractionalSecondDigits");
            if (f && !ts_value_is_undefined(f)) {
                uint64_t fnb=nanbox_from_tsvalue_ptr(f);
                if(nanbox_is_number(fnb)){ double dv=ts_to_number(f);
                    if(!(dv==dv)||std::isinf(dv)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fractionalSecondDigits must be \"auto\" or an integer 0-9")); }
                    long long fv=(long long)std::floor(dv);   // floor first, then range-check (9.7 -> 9 is valid)
                    if(fv<0||fv>9){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fractionalSecondDigits must be \"auto\" or an integer 0-9")); }
                    fsd=(int)fv;
                } else { std::string fs;
                    if(!option_to_string(f,&fs) || fs!="auto"){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fractionalSecondDigits must be \"auto\" or an integer 0-9")); }
                } } }
        if (smallest=="second"||smallest=="seconds") digits=0;
        else if (smallest=="millisecond"||smallest=="milliseconds") digits=3;
        else if (smallest=="microsecond"||smallest=="microseconds") digits=6;
        else if (smallest=="nanosecond"||smallest=="nanoseconds") digits=9;
        else if (fsd>=0) digits=fsd;
        if (digits>=0 && digits<9) {
            long long q=1; for(int i=0;i<9-digits;i++) q*=10;
            long long rounded = round_nonneg(frac, q, mode);
            sec += rounded/1000000000LL; frac = rounded%1000000000LL;
        }
    }
    bool anyTime = d->hours||d->minutes||sec||frac||(digits>0);
    if (anyTime) {
        out += "T";
        if (d->hours)   { snprintf(b,sizeof(b),"%lldH",u(d->hours));   out += b; }
        if (d->minutes) { snprintf(b,sizeof(b),"%lldM",u(d->minutes)); out += b; }
        if (sec || frac || digits>0) {
            snprintf(b,sizeof(b),"%lld",sec); out += b;
            if (digits>0) {
                char fb[16]; snprintf(fb,sizeof(fb),"%09lld",frac);
                out += "."; out.append(fb, digits);
            } else if (digits<0 && frac) {
                char fb[16]; snprintf(fb,sizeof(fb),"%09lld",frac);
                int len = 9; while (len>1 && fb[len-1]=='0') len--;
                out += "."; out.append(fb, len);
            }
            out += "S";
        }
    }
    if (out == "P" || out == "-P") out += "T0S";
    return TsString::Create(out.c_str());
}

extern "C" {

TsValue* ts_temporal_duration_toString_native(void* ctx, int argc, TsValue** argv) {
    TsDuration* d = require_duration(ctx, "toString");
    TsValue* opts = (argc>=1&&argv)?argv[0]:nullptr;
    require_options_object(opts);
    return ts_value_make_string(duration_iso_string(d, opts));
}

TsValue* ts_temporal_duration_valueOf_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "Called valueOf on a Temporal.Duration; use compare() instead"));
    return ts_value_make_undefined();
}

TsValue* ts_temporal_duration_negated_native(void* ctx, int argc, TsValue** argv) {
    TsDuration* d = require_duration(ctx, "negated");
    return ts_value_make_object(TsDuration::Create(-d->years,-d->months,-d->weeks,-d->days,-d->hours,
        -d->minutes,-d->seconds,-d->milliseconds,-d->microseconds,-d->nanoseconds));
}

TsValue* ts_temporal_duration_abs_native(void* ctx, int argc, TsValue** argv) {
    TsDuration* d = require_duration(ctx, "abs");
    auto a = [](long long v){ return v<0?-v:v; };
    return ts_value_make_object(TsDuration::Create(a(d->years),a(d->months),a(d->weeks),a(d->days),
        a(d->hours),a(d->minutes),a(d->seconds),a(d->milliseconds),a(d->microseconds),a(d->nanoseconds)));
}

TsValue* ts_temporal_duration_with_native(void* ctx, int argc, TsValue** argv) {
    TsDuration* d = require_duration(ctx, "with");
    TsValue* arg = (argc>=1&&argv)?argv[0]:nullptr;
    void* raw = arg ? ts_nanbox_safe_unbox(arg) : nullptr;
    if (!raw || *(uint32_t*)raw==0x53545247 || *(uint32_t*)raw==0x434F4E53) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.Duration.prototype.with: argument must be a plain object"));
        return ts_value_make_undefined();
    }
    static const char* names[10] = {"years","months","weeks","days","hours","minutes","seconds","milliseconds","microseconds","nanoseconds"};
    long long cur[10] = {d->years,d->months,d->weeks,d->days,d->hours,d->minutes,d->seconds,d->milliseconds,d->microseconds,d->nanoseconds};
    bool any=false, ok=true;
    for (int i=0;i<10;i++){
        TsValue* f = ts_object_get_property(raw, names[i]);
        if (f && !ts_value_is_undefined(f)) { any=true; cur[i]=duration_field(f,&ok); if(!ok) return ts_value_make_undefined(); }
    }
    if (!any) { ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if (!duration_in_range(cur)) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.with: a component is out of range")); return ts_value_make_undefined(); }
    if (!duration_same_sign(cur)) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.with: mixed signs")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsDuration::Create(cur[0],cur[1],cur[2],cur[3],cur[4],cur[5],cur[6],cur[7],cur[8],cur[9]));
}

TsValue* ts_temporal_duration_from_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx; return ts_temporal_duration_from(argc, argv);
}

} // extern "C"

// Parse an ISO-8601 duration. Returns false on malformed input.
static bool parse_iso_duration(const char* s, long long* f) {
    for (int i=0;i<10;i++) f[i]=0;
    const char* p = s;
    int sign = 1;
    if (*p=='+'||*p=='-') { if(*p=='-') sign=-1; p++; }
    if (*p!='P'&&*p!='p') return false;
    p++;
    bool inTime=false, anyField=false;
    while (*p) {
        if (*p=='T'||*p=='t') { inTime=true; p++; if(!isdigit((unsigned char)*p)) return false; continue; }  // T must be followed by a time component
        if (!isdigit((unsigned char)*p)) return false;
        long long val=0; while(isdigit((unsigned char)*p)){ val=val*10+(*p-'0'); p++; }
        long long fracNs=0; bool hasFrac=false;
        if (*p=='.'||*p==',') { hasFrac=true; p++; if(!isdigit((unsigned char)*p)) return false; char fb[10]="000000000"; int i=0; while(i<9&&isdigit((unsigned char)*p)){fb[i++]=*p++;} if(isdigit((unsigned char)*p)) return false; fracNs=atol(fb); }  // at most 9 fractional digits; a fraction needs >=1 digit
        char unit=*p; if(!unit) return false; p++;
        anyField=true;
        if (!inTime) {
            if (hasFrac) return false;
            switch(unit){ case 'Y':case 'y':f[0]=sign*val;break; case 'M':case 'm':f[1]=sign*val;break;
                case 'W':case 'w':f[2]=sign*val;break; case 'D':case 'd':f[3]=sign*val;break; default:return false; }
        } else {
            switch(unit){
                case 'H':case 'h': f[4]=sign*val;
                    if(hasFrac){ long long t=fracNs*3600LL; f[5]=sign*(t/60000000000LL); t%=60000000000LL; f[6]=sign*(t/1000000000LL); t%=1000000000LL; f[7]=sign*(t/1000000LL); t%=1000000LL; f[8]=sign*(t/1000LL); f[9]=sign*(t%1000LL); }
                    break;
                case 'M':case 'm': f[5]=sign*val;
                    if(hasFrac){ long long t=fracNs*60LL; f[6]=sign*(t/1000000000LL); t%=1000000000LL; f[7]=sign*(t/1000000LL); t%=1000000LL; f[8]=sign*(t/1000LL); f[9]=sign*(t%1000LL); }
                    break;
                case 'S':case 's':
                    f[6]=sign*val;
                    if (fracNs){ f[7]=sign*(fracNs/1000000); f[8]=sign*((fracNs/1000)%1000); f[9]=sign*(fracNs%1000); }
                    break;
                default: return false;
            }
        }
        // ISO 8601: a fraction is only allowed on the final (smallest) component.
        if (hasFrac && *p!=0) return false;
    }
    return anyField;
}

extern "C" TsValue* ts_temporal_duration_from(int argc, TsValue** argv) {
    TsValue* item = (argc>=1&&argv)?argv[0]:nullptr;
    if (!item || ts_value_is_undefined(item)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.from: argument is undefined"));
        return ts_value_make_undefined();
    }
    void* raw = ts_nanbox_safe_unbox(item);
    if (raw) {
        uint32_t m0 = *(uint32_t*)raw;
        if (m0==0x53545247 || m0==0x434F4E53) {
            const char* utf = ((TsString*)ts_value_get_string(item))->ToUtf8();
            long long f[10];
            if (!utf || !parse_iso_duration(utf, f)) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.from: invalid ISO duration string"));
                return ts_value_make_undefined();
            }
            if (!duration_in_range(f)) { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.from: a component is out of range")); return ts_value_make_undefined(); }
            return ts_value_make_object(TsDuration::Create(f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9]));
        }
        if (*(uint32_t*)((char*)raw+16)==TsDuration::MAGIC) {
            TsDuration* o = (TsDuration*)raw;
            return ts_value_make_object(TsDuration::Create(o->years,o->months,o->weeks,o->days,o->hours,o->minutes,o->seconds,o->milliseconds,o->microseconds,o->nanoseconds));
        }
        static const char* names[10] = {"years","months","weeks","days","hours","minutes","seconds","milliseconds","microseconds","nanoseconds"};
        long long f[10]={0,0,0,0,0,0,0,0,0,0}; bool any=false, ok=true;
        for (int i=0;i<10;i++){ TsValue* x=ts_object_get_property(raw,names[i]); if(x&&!ts_value_is_undefined(x)){any=true; f[i]=duration_field(x,&ok); if(!ok) return ts_value_make_undefined();} }
        if (!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.from: object has no recognized duration fields")); return ts_value_make_undefined(); }
        if (!duration_in_range(f)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.from: a component is out of range")); return ts_value_make_undefined(); }
        if (!duration_same_sign(f)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.from: mixed signs")); return ts_value_make_undefined(); }
        return ts_value_make_object(TsDuration::Create(f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9]));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.from: invalid argument"));
    return ts_value_make_undefined();
}

// ============================ Temporal.PlainDate ============================
static bool iso_is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
static int iso_days_in_month(int y, int m) {
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && iso_is_leap(y)) return 29;
    return (m >= 1 && m <= 12) ? dm[m-1] : 30;
}
// Days since 1970-01-01 (proleptic Gregorian; Howard Hinnant's algorithm).
static long long iso_days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe/4 - yoe/100 + doy;
    return era * 146097 + doe - 719468;
}
static int iso_day_of_week(int y, int m, int d) {  // ISO: Monday=1 .. Sunday=7
    long long days = iso_days_from_civil(y, m, d);  // 1970-01-01 = Thursday
    int w = (int)(((days + 3) % 7 + 7) % 7);  // 0=Mon .. 6=Sun
    return w + 1;
}
static int iso_day_of_year(int y, int m, int d) {
    return (int)(iso_days_from_civil(y, m, d) - iso_days_from_civil(y, 1, 1)) + 1;
}
static int iso_weeks_in_year(int y) {
    int a = iso_day_of_week(y, 1, 1);
    return (a == 4 || (a == 3 && iso_is_leap(y))) ? 53 : 52;
}

TsPlainDate* TsPlainDate::Create(int y, int m, int d) {
    void* mem = ts_alloc(sizeof(TsPlainDate));
    TsPlainDate* pd = new (mem) TsPlainDate();
    pd->magic = MAGIC;
    pd->iso_year = y; pd->iso_month = m; pd->iso_day = d;
    return pd;
}

TsValue TsPlainDate::GetPropertyVirtual(const char* key) {
    auto mkInt = [](long long v){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=v; return r; };
    auto mkBool = [](bool v){ TsValue r; r.type=ValueType::BOOLEAN; r.i_val=v?1:0; return r; };
    auto mkStr = [](const char* s){ TsValue r; r.type=ValueType::STRING_PTR; r.ptr_val=TsString::Create(s); return r; };
    TsValue undef; undef.type=ValueType::UNDEFINED; undef.i_val=0;
    if (strcmp(key,"year")==0) return mkInt(iso_year);
    if (strcmp(key,"month")==0) return mkInt(iso_month);
    if (strcmp(key,"day")==0) return mkInt(iso_day);
    if (strcmp(key,"calendarId")==0) return mkStr("iso8601");
    if (strcmp(key,"dayOfWeek")==0) return mkInt(iso_day_of_week(iso_year,iso_month,iso_day));
    if (strcmp(key,"dayOfYear")==0) return mkInt(iso_day_of_year(iso_year,iso_month,iso_day));
    if (strcmp(key,"daysInWeek")==0) return mkInt(7);
    if (strcmp(key,"daysInMonth")==0) return mkInt(iso_days_in_month(iso_year,iso_month));
    if (strcmp(key,"daysInYear")==0) return mkInt(iso_is_leap(iso_year)?366:365);
    if (strcmp(key,"monthsInYear")==0) return mkInt(12);
    if (strcmp(key,"inLeapYear")==0) return mkBool(iso_is_leap(iso_year));
    if (strcmp(key,"monthCode")==0) { char b[8]; snprintf(b,sizeof(b),"M%02d",iso_month); return mkStr(b); }
    if (strcmp(key,"weekOfYear")==0 || strcmp(key,"yearOfWeek")==0) {
        int isoDow = iso_day_of_week(iso_year,iso_month,iso_day);
        int ordinal = iso_day_of_year(iso_year,iso_month,iso_day);
        int week = (ordinal - isoDow + 10) / 7;
        int yow = iso_year;
        if (week < 1) { yow = iso_year-1; week = iso_weeks_in_year(iso_year-1); }
        else if (week > iso_weeks_in_year(iso_year)) { yow = iso_year+1; week = 1; }
        return (key[0]=='w') ? mkInt(week) : mkInt(yow);
    }
    if (strcmp(key,"era")==0 || strcmp(key,"eraYear")==0) return undef;  // iso8601 has none
    return undef;
}

static TsPlainDate* as_plaindate(void* raw) {
    if (!raw) return nullptr;
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 || m0 == 0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw + 16) == TsPlainDate::MAGIC) ? (TsPlainDate*)raw : nullptr;
}

extern "C" {
    void* ts_temporal_get_plaindate_ctor();
    TsValue* ts_temporal_plaindate_from(int argc, TsValue** argv);
}

static TsPlainDate* require_plaindate(void* ctx, const char* method) {
    if (!ctx) ctx = ts_get_call_this();
    TsPlainDate* d = as_plaindate(ts_nanbox_safe_unbox(ctx));
    if (!d) {
        std::string msg = std::string("Temporal.PlainDate.prototype.") + method +
            " called on an object that is not a Temporal.PlainDate";
        ts_throw((TsValue*)ts_error_create_typed("TypeError", msg.c_str()));
    }
    return d;
}

// RejectISODate + a coarse year-range check.
static bool iso_date_valid(int y, int m, int d) {
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > iso_days_in_month(y, m)) return false;
    if (y < -271821 || y > 275760) return false;
    return true;
}
// Temporal representable range: the ISO date must fall within
// [-271821-04-19, +275760-09-13] (PlainDate). For a PlainDateTime the lower
// bound is exclusive of midnight on the first day (the first representable
// instant is -271821-04-19T00:00:00.000000001).
static bool iso_date_in_limits(int y, int m, int d) {
    static const long long DMIN = iso_days_from_civil(-271821, 4, 19);
    static const long long DMAX = iso_days_from_civil(275760, 9, 13);
    long long days = iso_days_from_civil(y, m, d);
    return days >= DMIN && days <= DMAX;
}
static bool iso_datetime_in_limits(int y, int m, int d, long long timeNs) {
    static const long long DMIN = iso_days_from_civil(-271821, 4, 19);
    static const long long DMAX = iso_days_from_civil(275760, 9, 13);
    long long days = iso_days_from_civil(y, m, d);
    if (days > DMAX) return false;
    if (days < DMIN) return false;
    if (days == DMIN) return timeNs > 0;
    return true;
}
// An Instant is representable when |epochNanoseconds| <= 8.64e21, i.e.
// |epoch_ms| <= 8.64e18 (with sub-ms ns 0 at the positive boundary).
static bool instant_epoch_in_limits(long long ms, int subNs) {
    const long long MAXMS = 8640000000000000LL; // 8.64e21 ns / 1e6 ns-per-ms = 8.64e15 ms
    if (ms > MAXMS) return false;
    if (ms == MAXMS && subNs > 0) return false;
    if (ms < -MAXMS) return false;
    if (ms == -MAXMS && subNs < 0) return false;   // TRUNC storage just past the min
    return true;
}
// A PlainYearMonth is representable when any day of the month is in range.
static bool iso_yearmonth_in_limits(int y, int m) {
    static const long long DMIN = iso_days_from_civil(-271821, 4, 19);
    static const long long DMAX = iso_days_from_civil(275760, 9, 13);
    if (m < 1 || m > 12) return false;
    long long first = iso_days_from_civil(y, m, 1);
    long long last = iso_days_from_civil(y, m, iso_days_in_month(y, m));
    return last >= DMIN && first <= DMAX;
}

// The constructor `calendar` argument is ToString'd then canonicalized: only the
// ASCII-case-insensitive identifier "iso8601" is supported. Empty string, an ISO
// date string, a non-ASCII-dotted form (Turkish capital I), etc. are RangeErrors.
static bool string_calendar_is_iso(const char* s);
static bool parse_iso_yearmonth(const char* s, int* Y, int* M);
static bool parse_iso_monthday(const char* s, int* M, int* D);
// Constructor calendar argument: ToTemporalCalendarIdentifier — a bare calendar
// ID only ("iso8601"); an ISO string (even with [u-ca=iso8601]) is NOT accepted.
// A calendar identifier must be a string (ToTemporalCalendarIdentifier); a
// non-string (null/number/bigint/symbol/object) is a TypeError.
static bool calendar_arg_is_string(TsValue* v){
    void* raw=v?ts_nanbox_safe_unbox(v):nullptr;
    if(!raw || (uintptr_t)raw<4096 || (uintptr_t)raw>0x00007FFFFFFFFFFFULL) return false;
    return *(uint32_t*)raw==0x53545247 || *(uint32_t*)raw==0x434F4E53;
}
static void validate_iso_calendar_arg(TsValue* v){
    if(!v || ts_value_is_undefined(v)) return;
    if(!calendar_arg_is_string(v)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal: calendar must be a string")); return; }
    std::string cal;
    if(!tsvalue_to_stdstring(v,&cal)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: invalid calendar")); return; }
    for(char& c: cal) if(c>='A'&&c<='Z') c=(char)(c+32);
    if(cal!="iso8601"){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: invalid calendar identifier")); }
}
// withCalendar calendar argument: ToTemporalCalendarSlotValue — accepts a bare
// calendar ID OR an ISO date/datetime/yearmonth/monthday string whose calendar
// annotation is iso8601.
// The string branch lives in its own function so the std::string locals never
// share a frame with the early TypeError longjmp below — a longjmp out of a
// std::string-bearing frame (when called directly from an extern "C" native, as
// in ZonedDateTime.withCalendar) corrupts the unwinder on MSVC.
static void validate_calendar_string_value(TsValue* v){
    std::string cal;
    if(!tsvalue_to_stdstring(v,&cal)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: invalid calendar")); return; }
    std::string low=cal; for(char& c: low) if(c>='A'&&c<='Z') c=(char)(c+32);
    if(low=="iso8601") return;
    const char* s=cal.c_str();
    int a,b,c2,d,e,f,g,h,i2;
    bool isoStr = parse_iso_datetime(s,&a,&b,&c2,&d,&e,&f,&g,&h,&i2)
               || parse_iso_yearmonth(s,&a,&b)
               || parse_iso_monthday(s,&a,&b);
    if(isoStr && iso_annotations_valid(s) && string_calendar_is_iso(s)) return;
    ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: invalid calendar identifier"));
}
static void validate_calendar_slot_arg(TsValue* v){
    // withCalendar requires a calendar argument; missing/undefined is a TypeError.
    if(!v || ts_value_is_undefined(v)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal: a calendar is required")); return; }
    // A date-bearing Temporal object (PlainDate/PlainDateTime/PlainYearMonth/
    // PlainMonthDay/ZonedDateTime) supplies its own (iso) calendar — accept it.
    void* raw=ts_nanbox_safe_unbox(v);
    if(raw && (uintptr_t)raw>=4096 && (uintptr_t)raw<=0x00007FFFFFFFFFFFULL){ uint32_t m16=*(uint32_t*)((char*)raw+16);
        if(m16==0x504C4454||m16==0x504C594D||m16==0x504C4D44||m16==0x50444D54||m16==0x5A44544D) return; }
    if(!calendar_arg_is_string(v)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal: calendar must be a string")); return; }
    validate_calendar_string_value(v);
}
extern "C" TsValue* ts_temporal_plaindate_construct(int argc, TsValue** argv) {
    auto field = [&](int i, bool* ok) -> int {
        if (i >= argc || !argv || !argv[i] || ts_value_is_undefined(argv[i])) { *ok=false; return 0; }
        double dd = ts_to_number(argv[i]);
        if (dd != dd || std::isinf(dd)) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate: field not finite"));
            return 0;
        }
        return (int)std::trunc(dd);
    };
    bool oy=true, om=true, od=true;
    int y = field(0,&oy), m = field(1,&om), d = field(2,&od);
    // year/month/day are required (no defaults in the constructor).
    if (!oy || !om || !od) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate: year, month and day are required"));
        return ts_value_make_undefined();
    }
    if (!iso_date_valid(y, m, d) || !iso_date_in_limits(y, m, d)) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate: date out of range"));
        return ts_value_make_undefined();
    }
    validate_iso_calendar_arg((argc>=4&&argv)?argv[3]:nullptr);
    return ts_value_make_object(TsPlainDate::Create(y, m, d));
}

static TsString* plaindate_iso_string(TsPlainDate* d) {
    char buf[24];
    if (d->iso_year < 0 || d->iso_year > 9999)
        snprintf(buf,sizeof(buf), "%+07d-%02d-%02d", d->iso_year, d->iso_month, d->iso_day);
    else
        snprintf(buf,sizeof(buf), "%04d-%02d-%02d", d->iso_year, d->iso_month, d->iso_day);
    return TsString::Create(buf);
}

// Parse "YYYY-MM-DD" (optionally a longer datetime; takes the date portion).
// Validate trailing ISO annotations ([key=value], [!key=value] critical, [tz]).
// Rules: at most one calendar (u-ca) and one time-zone annotation; lowercase keys;
// no unknown annotation with the critical (!) flag; non-empty calendar value.
static bool iso_annotations_valid(const char* s){
    int calCount=0, tzCount=0; bool calCritical=false;
    const char* p=strchr(s,'[');
    while(p){
        const char* end=strchr(p,']'); if(!end) return false;
        if(end[1]!=0 && end[1]!='[') return false;   // only another annotation or end may follow ']'
        std::string ann(p+1, (size_t)(end-p-1));
        bool critical = !ann.empty() && ann[0]=='!';
        std::string body = critical ? ann.substr(1) : ann;
        size_t eq = body.find('=');
        if(eq!=std::string::npos){
            std::string key=body.substr(0,eq), val=body.substr(eq+1);
            for(char c: key) if(c>='A'&&c<='Z') return false;  // keys must be lowercase
            // Multiple calendar annotations are allowed (first wins, rest ignored)
            // UNLESS two or more appear and ANY of them is critical — a critical
            // annotation must not be silently ignored.
            if(key=="u-ca"){ if(val.empty()) return false; calCount++; if(critical) calCritical=true; if(calCount>1 && calCritical) return false; }
            else if(critical) return false;                    // unknown critical annotation
        } else {
            if(++tzCount>1) return false;                      // >1 time-zone annotation
            // A bracketed offset time-zone identifier is minute precision only
            // (±HH:MM); a trailing ":SS"/".fff" sub-minute part is invalid.
            if(!body.empty() && (body[0]=='+'||body[0]=='-')){
                const char* q=body.c_str()+1;
                if(!isdigit((unsigned char)q[0])||!isdigit((unsigned char)q[1])) return false; q+=2;
                if(*q==':')q++;
                if(isdigit((unsigned char)q[0])&&isdigit((unsigned char)q[1])) q+=2;
                if(*q!=0) return false;                         // sub-minute / junk
            }
        }
        p=strchr(end+1,'[');
    }
    return true;
}
// True unless the string carries a non-ISO calendar annotation ("[u-ca=hebrew]").
// Only the calendar-bearing date types that cannot ignore it call this (Instant /
// PlainTime have no calendar and silently ignore the annotation).
static bool string_calendar_is_iso(const char* s){
    const char* p=strchr(s,'[');
    while(p){ const char* e=strchr(p,']'); if(!e) break;
        std::string ann(p+1,(size_t)(e-p-1));
        if(!ann.empty()&&ann[0]=='!') ann=ann.substr(1);
        if(ann.compare(0,5,"u-ca=")==0){ std::string v=ann.substr(5);
            for(char& c:v) if(c>='A'&&c<='Z') c=(char)(c+32);
            return v=="iso8601"; }   // only the FIRST calendar annotation matters
        p=strchr(e+1,'['); }
    return true;
}
// Read the month from a property bag: prefer numeric "month", else "monthCode"
// ("M01".."M12"). Returns -1 if neither present/valid.
static int read_bag_month(void* raw){
    int fromMonth=-1;
    TsValue* fm=ts_object_get_property(raw,"month");
    if(fm && !ts_value_is_undefined(fm)){ double d=ts_to_number(fm);
        if(std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: month property cannot be Infinity")); }
        if(d==d) fromMonth=(int)std::trunc(d); }
    int fromCode=-1;
    TsValue* mc=ts_object_get_property(raw,"monthCode");
    if(mc && !ts_value_is_undefined(mc)){
        // ECMA: monthCode is "M" + two digits + optional "L" (leap). The ISO
        // calendar has no leap months, so a trailing "L" — and any malformed or
        // out-of-range code — is a RangeError (not a silent -> needs-month/day).
        std::string s; bool strok=tsvalue_to_stdstring(mc,&s);
        bool fmt = strok && (s.size()==3||s.size()==4) && s[0]=='M'
                   && s[1]>='0'&&s[1]<='9' && s[2]>='0'&&s[2]<='9'
                   && (s.size()==3 || s[3]=='L');
        int m = fmt ? (s[1]-'0')*10+(s[2]-'0') : 0;
        bool hasL = fmt && s.size()==4;
        if(!fmt || hasL || m<1 || m>12){
            ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: invalid monthCode"));
        }
        fromCode=m;
    }
    // When both month and monthCode are given they must agree (ECMA
    // ISOMonthCode / CalendarResolveFields: conflicting month vs monthCode throws).
    if(fromMonth>=1 && fromCode>=1 && fromMonth!=fromCode){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: month and monthCode conflict"));
    }
    if(fromCode>=1) return fromCode;
    return fromMonth;
}
static bool parse_iso_date(const char* s, int* Y, int* M, int* D) {
    if (has_unicode_minus(s)) return false;
    int sign = 1; const char* p = s;
    if (*p=='+'||*p=='-') { if(*p=='-') sign=-1; p++; }
    int y=0;
    // Fixed-width year (4, or signed 6) so basic format YYYYMMDD parses.
    bool _sgn=(s[0]=='+'||s[0]=='-'); int ywidth=_sgn?6:4;
    for(int i=0;i<ywidth;i++){ if(!isdigit((unsigned char)*p)) return false; y=y*10+(*p-'0'); p++; }
    // ECMA-262: a minus-signed extended year of zero ("-000000") is invalid.
    if (sign<0 && y==0) return false;
    if (*p=='-') p++;
    if (!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    if (*p=='-') p++;
    if (!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int da=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    *Y=sign*y; *M=mo; *D=da;
    return true;
}
// Like parse_iso_date, but also reports where the date ends (*endOut).
static bool parse_iso_date_e(const char* s, int* Y, int* M, int* D, const char** endOut) {
    if (has_unicode_minus(s)) return false;
    int sign = 1; const char* p = s;
    if (*p=='+'||*p=='-') { if(*p=='-') sign=-1; p++; }
    int y=0;
    // Fixed-width year (4, or signed 6) so basic format YYYYMMDD parses — a greedy
    // read would swallow the MM/DD digits.
    bool _sgn=(s[0]=='+'||s[0]=='-'); int ywidth=_sgn?6:4;
    for(int i=0;i<ywidth;i++){ if(!isdigit((unsigned char)*p)) return false; y=y*10+(*p-'0'); p++; }
    if (sign<0 && y==0) return false;
    if (*p=='-') p++;
    if (!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    if (*p=='-') p++;
    if (!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int da=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    *Y=sign*y; *M=mo; *D=da; *endOut=p;
    return true;
}
// After a YYYY-MM-DD date, the only valid continuations for a date-bearing type
// are: end of string, a '[' annotation, or a 'T' DesignatedTime. A bare UTC
// offset ("2022-09-15+00:00") or a trailing 'T' with no time ("2020-01-01T") is
// NOT a valid date string (an offset/Z is meaningless without a time).
static bool date_string_suffix_ok(const char* s){
    const char* p=s; if(*p=='+'||*p=='-')p++;
    while(isdigit((unsigned char)*p))p++;            // year (4+ digits)
    if(*p=='-')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1]))p+=2;  // month
    if(*p=='-')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1]))p+=2;  // day
    if(*p=='\0' || *p=='[') return true;
    // 'T', 't', or a space all introduce a DesignatedTime (which must start with HH).
    if(*p=='T'||*p=='t'||*p==' ') return isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]);
    return false;  // bare offset / junk directly after the date
}
static bool parse_iso_yearmonth(const char* s, int* Y, int* M);
static bool parse_iso_monthday(const char* s, int* M, int* D);
// Validate a property-bag "calendar" field: absent / a Temporal object / "iso8601"
// (case-insensitive) / an ISO string carrying [u-ca=iso8601] / a parseable date
// string are OK; any other string is an invalid calendar -> caller throws RangeError.
static bool bag_calendar_ok(void* raw){
    TsValue* cf=ts_object_get_property(raw,"calendar");
    if(!cf||ts_value_is_undefined(cf)) return true;
    void* cr=ts_nanbox_safe_unbox(cf);
    if(cr){
        // A Temporal date-like object carries its own calendar slot (magic at +16).
        uint32_t m16=*(uint32_t*)((char*)cr+16);
        if(m16==TsPlainDate::MAGIC||m16==TsPlainDateTime::MAGIC||m16==TsPlainYearMonth::MAGIC||
           m16==TsPlainMonthDay::MAGIC||m16==TsZonedDateTime::MAGIC) return true;
        uint32_t m0=*(uint32_t*)cr;
        if(m0==0x53545247||m0==0x434F4E53){  // string calendar value -> validate
            std::string s; tsvalue_to_stdstring(cf,&s);
            for(char& c:s) if(c>='A'&&c<='Z') c+=32;
            if(s=="iso8601") return true;
            if(s.find("[u-ca=iso8601]")!=std::string::npos || s.find("[!u-ca=iso8601]")!=std::string::npos) return true;
            // Any parseable ISO temporal string (date / datetime / year-month /
            // month-day) yields the iso8601 calendar.
            int Y,M,D; if(parse_iso_date(s.c_str(),&Y,&M,&D) && iso_date_valid(Y,M,D)) return true;
            if(parse_iso_yearmonth(s.c_str(),&Y,&M) && M>=1 && M<=12) return true;
            if(parse_iso_monthday(s.c_str(),&M,&D) && M>=1 && M<=12 && D>=1 && D<=31) return true;
            return false;  // invalid string -> caller throws RangeError
        }
    }
    // Any other type (null/boolean/number/bigint/symbol/plain or non-date Temporal
    // object such as Duration) is not a valid calendar -> TypeError.
    ts_throw((TsValue*)ts_error_create_typed("TypeError","calendar must be a string or a calendar-bearing Temporal object"));
    return false;
}

extern "C" {

// Append [u-ca=iso8601] (or [!...] for critical) when calendarName is
// always/critical; otherwise return the base string unchanged.
static TsValue* append_cal_annotation(TsString* base, TsValue* opts){
    static const char* CALV[]={"auto","always","never","critical"};
    std::string cal = read_enum_option(opts, "calendarName", "auto", CALV, 4);
    if(cal!="always" && cal!="critical") return ts_value_make_string(base);
    const char* u = base ? base->ToUtf8() : "";
    std::string s = u ? u : "";
    s += (cal=="critical") ? "[!u-ca=iso8601]" : "[u-ca=iso8601]";
    return ts_value_make_string(TsString::Create(s.c_str()));
}

TsValue* ts_temporal_plaindate_toString_native(void* ctx, int argc, TsValue** argv) {
    TsPlainDate* d = require_plaindate(ctx, "toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    return append_cal_annotation(plaindate_iso_string(d), (argc>=1&&argv)?argv[0]:nullptr);
}

TsValue* ts_temporal_plaindate_valueOf_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    ts_throw((TsValue*)ts_error_create_typed("TypeError",
        "Called valueOf on a Temporal.PlainDate; use compare() or equals() instead"));
    return ts_value_make_undefined();
}

TsValue* ts_temporal_plaindate_equals_native(void* ctx, int argc, TsValue** argv) {
    TsPlainDate* a = require_plaindate(ctx, "equals");
    TsValue* other = (argc>=1&&argv)?argv[0]:nullptr;
    TsPlainDate* b = coerce_plaindate_arg(other);
    if (!b) return ts_value_make_bool(false);
    return ts_value_make_bool(a->iso_year==b->iso_year && a->iso_month==b->iso_month && a->iso_day==b->iso_day);
}

TsValue* ts_temporal_plaindate_compare_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    TsPlainDate* a = coerce_plaindate_arg((argc>=1)?argv[0]:nullptr);
    TsPlainDate* b = coerce_plaindate_arg((argc>=2)?argv[1]:nullptr);
    if (!a || !b) return ts_value_make_int(0);
    int af[3]={a->iso_year,a->iso_month,a->iso_day}, bf[3]={b->iso_year,b->iso_month,b->iso_day};
    for (int i=0;i<3;i++){ if(af[i]<bf[i]) return ts_value_make_int(-1); if(af[i]>bf[i]) return ts_value_make_int(1); }
    return ts_value_make_int(0);
}

TsValue* ts_temporal_plaindate_with_native(void* ctx, int argc, TsValue** argv) {
    TsPlainDate* pd = require_plaindate(ctx, "with");
    bool _ovrej = validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr);
    TsValue* arg = (argc>=1&&argv)?argv[0]:nullptr;
    void* raw = arg?ts_nanbox_safe_unbox(arg):nullptr;
    if (!raw || *(uint32_t*)raw==0x53545247 || *(uint32_t*)raw==0x434F4E53 ||
        *(uint32_t*)((char*)raw+16)==TsPlainDate::MAGIC) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainDate.prototype.with: argument must be a plain object"));
        return ts_value_make_undefined();
    }
    int vals[3] = {pd->iso_year, pd->iso_month, pd->iso_day};
    static const char* names[3] = {"year","month","day"};
    bool any=false;
    for (int i=0;i<3;i++){
        TsValue* f = ts_object_get_property(raw, names[i]);
        if (f && !ts_value_is_undefined(f)) {
            any=true; double dd=ts_to_number(f);
            if (dd!=dd||std::isinf(dd)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite")); return ts_value_make_undefined(); }
            vals[i]=(int)std::trunc(dd);
        }
    }
    if (!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if (_ovrej && (vals[1]<1||vals[1]>12||vals[2]<1||vals[2]>iso_days_in_month(vals[0],vals[1]))){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.with: field out of range (overflow reject)")); return ts_value_make_undefined(); }
    // constrain month then day
    if (vals[1]<1) vals[1]=1; if (vals[1]>12) vals[1]=12;
    int dim = iso_days_in_month(vals[0], vals[1]);
    if (vals[2]<1) vals[2]=1; if (vals[2]>dim) vals[2]=dim;
    if (!iso_date_valid(vals[0],vals[1],vals[2])){ ts_throw((TsValue*)ts_error_create_typed("RangeError","date out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(vals[0],vals[1],vals[2]));
}

TsValue* ts_temporal_plaindate_from_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx; return ts_temporal_plaindate_from(argc, argv);
}

} // extern "C"

extern "C" TsValue* ts_temporal_plaindate_from(int argc, TsValue** argv) {
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    // The overflow option must be READ only after the input has been parsed/validated
    // (an invalid ISO string throws before options are observed). _ovopt reads it.
    auto _ovopt=[&]()->bool{ return validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); };
    TsValue* item = (argc>=1&&argv)?argv[0]:nullptr;
    if (!item || ts_value_is_undefined(item)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.from: argument is undefined"));
        return ts_value_make_undefined();
    }
    void* raw = ts_nanbox_safe_unbox(item);
    if (raw) {
        // A ZonedDateTime / PlainDateTime supplies its date from its internal slot —
        // no observable property reads.
        if((uintptr_t)raw>=4096 && (uintptr_t)raw<=0x00007FFFFFFFFFFFULL){
            uint32_t m16f=*(uint32_t*)((char*)raw+16);
            if(m16f==0x5A44544D){ TsZonedDateTime* z=(TsZonedDateTime*)raw; int Y2,M2,D2,h2,mi2,s2,ms2,us2,ns2; zdt_local(z,&Y2,&M2,&D2,&h2,&mi2,&s2,&ms2,&us2,&ns2); _ovopt(); return ts_value_make_object(TsPlainDate::Create(Y2,M2,D2)); }
            if(m16f==0x50444D54){ TsPlainDateTime* dt=(TsPlainDateTime*)raw; _ovopt(); return ts_value_make_object(TsPlainDate::Create(dt->iso_year,dt->iso_month,dt->iso_day)); }
        }
        uint32_t m0 = *(uint32_t*)raw;
        if (m0==0x53545247 || m0==0x434F4E53) {
            const char* utf = ((TsString*)ts_value_get_string(item))->ToUtf8();
            int Y,M,D; int Yd,Md,Dd,hd,mid,sd,msd,usd,nsd;
            if (!utf || has_utc_designator(utf) || !parse_iso_date(utf,&Y,&M,&D) || !date_string_suffix_ok(utf) || !parse_iso_datetime(utf,&Yd,&Md,&Dd,&hd,&mid,&sd,&msd,&usd,&nsd) || !iso_date_valid(Y,M,D) || !iso_date_in_limits(Y,M,D) || !iso_annotations_valid(utf) || !string_calendar_is_iso(utf)) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.from: invalid ISO date string"));
                return ts_value_make_undefined();
            }
            _ovopt();   // valid string: overflow is read (observable) after parsing
            return ts_value_make_object(TsPlainDate::Create(Y,M,D));
        }
        if (*(uint32_t*)((char*)raw+16)==TsPlainDate::MAGIC) {
            TsPlainDate* o = (TsPlainDate*)raw;
            _ovopt();   // overflow is read (and validated) even for a PlainDate argument
            return ts_value_make_object(TsPlainDate::Create(o->iso_year,o->iso_month,o->iso_day));
        }
        // property bag: year/month(or monthCode)/day all required.
        TsValue* fy = ts_object_get_property(raw,"year");
        TsValue* fd = ts_object_get_property(raw,"day");
        bool hasY = fy && !ts_value_is_undefined(fy);
        int bagM = read_bag_month(raw);
        bool hasD = fd && !ts_value_is_undefined(fd);
        if (!hasY || bagM<1 || !hasD) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.from: object needs year, month and day"));
            return ts_value_make_undefined();
        }
        if(!bag_calendar_ok(raw)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.from: invalid calendar")); return ts_value_make_undefined(); }
        bool _ovrej = _ovopt();   // overflow read after the bag fields
        double dy=ts_to_number(fy), dd=ts_to_number(fd);
        if (dy!=dy||dd!=dd||std::isinf(dy)||std::isinf(dd)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite")); return ts_value_make_undefined(); }
        int Y=(int)std::trunc(dy), M=bagM, D=(int)std::trunc(dd);
        if (_ovrej && (M<1||M>12||D<1||D>iso_days_in_month(Y,M))){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.from: field out of range (overflow reject)")); return ts_value_make_undefined(); }
        // from default overflow constrain
        if (M<1) M=1; if (M>12) M=12;
        int dim=iso_days_in_month(Y,M); if (D<1) D=1; if (D>dim) D=dim;
        if (!iso_date_valid(Y,M,D) || !iso_date_in_limits(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","date out of range")); return ts_value_make_undefined(); }
        return ts_value_make_object(TsPlainDate::Create(Y,M,D));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.from: invalid argument"));
    return ts_value_make_undefined();
}

// ====================== Temporal.PlainYearMonth ======================
TsPlainYearMonth* TsPlainYearMonth::Create(int y, int m, int refDay) {
    void* mem = ts_alloc(sizeof(TsPlainYearMonth));
    TsPlainYearMonth* o = new (mem) TsPlainYearMonth();
    o->magic = MAGIC; o->iso_year = y; o->iso_month = m; o->iso_day = refDay;
    return o;
}
TsValue TsPlainYearMonth::GetPropertyVirtual(const char* key) {
    auto mkInt=[](long long v){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=v; return r; };
    auto mkBool=[](bool v){ TsValue r; r.type=ValueType::BOOLEAN; r.i_val=v?1:0; return r; };
    auto mkStr=[](const char* s){ TsValue r; r.type=ValueType::STRING_PTR; r.ptr_val=TsString::Create(s); return r; };
    TsValue undef; undef.type=ValueType::UNDEFINED; undef.i_val=0;
    if (strcmp(key,"year")==0) return mkInt(iso_year);
    if (strcmp(key,"month")==0) return mkInt(iso_month);
    if (strcmp(key,"calendarId")==0) return mkStr("iso8601");
    if (strcmp(key,"daysInMonth")==0) return mkInt(iso_days_in_month(iso_year,iso_month));
    if (strcmp(key,"daysInYear")==0) return mkInt(iso_is_leap(iso_year)?366:365);
    if (strcmp(key,"monthsInYear")==0) return mkInt(12);
    if (strcmp(key,"inLeapYear")==0) return mkBool(iso_is_leap(iso_year));
    if (strcmp(key,"monthCode")==0) { char b[8]; snprintf(b,sizeof(b),"M%02d",iso_month); return mkStr(b); }
    return undef;  // era/eraYear and anything else
}
static TsPlainYearMonth* as_plainyearmonth(void* raw) {
    if (!raw) return nullptr;
    uint32_t m0=*(uint32_t*)raw;
    if (m0==0x53545247||m0==0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw+16)==TsPlainYearMonth::MAGIC)?(TsPlainYearMonth*)raw:nullptr;
}
extern "C" { void* ts_temporal_get_plainyearmonth_ctor(); TsValue* ts_temporal_plainyearmonth_from(int argc, TsValue** argv); }
static TsPlainYearMonth* require_plainyearmonth(void* ctx, const char* method) {
    if (!ctx) ctx = ts_get_call_this();
    TsPlainYearMonth* d = as_plainyearmonth(ts_nanbox_safe_unbox(ctx));
    if (!d) { std::string msg=std::string("Temporal.PlainYearMonth.prototype.")+method+" called on an object that is not a Temporal.PlainYearMonth"; ts_throw((TsValue*)ts_error_create_typed("TypeError",msg.c_str())); }
    return d;
}
extern "C" TsValue* ts_temporal_plainyearmonth_construct(int argc, TsValue** argv) {
    auto fld=[&](int i,bool* ok)->int{ if(i>=argc||!argv||!argv[i]||ts_value_is_undefined(argv[i])){*ok=false;return 0;} double d=ts_to_number(argv[i]); if(d!=d||std::isinf(d)){ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: field not finite"));return 0;} return (int)std::trunc(d); };
    bool oy=true,om=true,ord=true; int y=fld(0,&oy),m=fld(1,&om);
    validate_iso_calendar_arg((argc>=3&&argv)?argv[2]:nullptr);   // calendar validated after month, before the reference day
    int refDay=fld(3,&ord); if(!ord) refDay=1;
    if(!oy||!om){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: year and month are required")); return ts_value_make_undefined(); }
    if(m<1||m>12||!iso_date_valid(y,m,refDay)||!iso_yearmonth_in_limits(y,m)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainYearMonth::Create(y,m,refDay));
}
static bool parse_iso_yearmonth(const char* s, int* Y, int* M) {
    if (has_unicode_minus(s)) return false;
    int sign=1; const char* p=s; if(*p=='+'||*p=='-'){if(*p=='-')sign=-1;p++;}
    int y=0,nd=0; while(isdigit((unsigned char)*p)){y=y*10+(*p-'0');p++;nd++;} if(nd<4) return false;
    if(sign<0 && y==0) return false;  // reject minus-zero extended year
    if(*p=='-')p++; if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); p+=2; *Y=sign*y; *M=mo;
    // A date-only year-month string must carry no UTC offset (Z / ±HH:MM): after
    // YYYY-MM only an optional "-DD" day and a '[' annotation may follow. A "-DD"
    // followed by ':' or a third digit is actually an offset, not a day. "Time
    // present" is a 'T'/'t' BEFORE the first '[' (a [UTC] annotation contains a 'T').
    { const char* br=strchr(s,'['); size_t dtlen=br?(size_t)(br-s):strlen(s);
      bool hasTime=false; for(size_t i=0;i<dtlen;i++) if(s[i]=='T'||s[i]=='t'||s[i]==' '){ hasTime=true; break; }
      if(!hasTime){
          if(p[0]=='-' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2])
             && p[3]!=':' && !isdigit((unsigned char)p[3])) p+=3;   // consume -DD
          if(*p!=0 && *p!='[') return false;
      } }
    return true;
}
extern "C" {
TsValue* ts_temporal_plainyearmonth_toString_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* d=require_plainyearmonth(ctx,"toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    static const char* CALV[]={"auto","always","never","critical"};
    std::string cal = read_enum_option((argc>=1&&argv)?argv[0]:nullptr, "calendarName", "auto", CALV, 4);
    bool showCal = (cal=="always"||cal=="critical");
    const char* ann = (cal=="critical") ? "[!u-ca=iso8601]" : "[u-ca=iso8601]";
    char b[48];
    if(showCal){
        // calendarName always/critical: include the reference ISO day + annotation.
        if(d->iso_year<0||d->iso_year>9999) snprintf(b,sizeof(b),"%+07d-%02d-%02d%s",d->iso_year,d->iso_month,d->iso_day,ann);
        else snprintf(b,sizeof(b),"%04d-%02d-%02d%s",d->iso_year,d->iso_month,d->iso_day,ann);
    } else {
        if(d->iso_year<0||d->iso_year>9999) snprintf(b,sizeof(b),"%+07d-%02d",d->iso_year,d->iso_month);
        else snprintf(b,sizeof(b),"%04d-%02d",d->iso_year,d->iso_month);
    }
    return ts_value_make_string(TsString::Create(b));
}
TsValue* ts_temporal_plainyearmonth_valueOf_native(void* ctx,int argc,TsValue** argv){ (void)ctx; ts_throw((TsValue*)ts_error_create_typed("TypeError","Called valueOf on a Temporal.PlainYearMonth; use compare() or equals() instead")); return ts_value_make_undefined(); }
TsValue* ts_temporal_plainyearmonth_equals_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* a=require_plainyearmonth(ctx,"equals"); TsValue* o=(argc>=1&&argv)?argv[0]:nullptr;
    TsPlainYearMonth* b=as_plainyearmonth(o?ts_nanbox_safe_unbox(o):nullptr);
    if(!b){ TsValue* c=ts_temporal_plainyearmonth_from(o?1:0,&o); b=as_plainyearmonth(ts_nanbox_safe_unbox(c)); if(!b) return ts_value_make_bool(false); }
    return ts_value_make_bool(a->iso_year==b->iso_year&&a->iso_month==b->iso_month&&a->iso_day==b->iso_day);
}
TsValue* ts_temporal_plainyearmonth_compare_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; auto to=[](TsValue* v)->TsPlainYearMonth*{ TsPlainYearMonth* p=as_plainyearmonth(v?ts_nanbox_safe_unbox(v):nullptr); if(p)return p; TsValue* c=ts_temporal_plainyearmonth_from(v?1:0,&v); return as_plainyearmonth(ts_nanbox_safe_unbox(c)); };
    TsPlainYearMonth* a=to((argc>=1)?argv[0]:nullptr),*b=to((argc>=2)?argv[1]:nullptr); if(!a||!b) return ts_value_make_int(0);
    int af[3]={a->iso_year,a->iso_month,a->iso_day},bf[3]={b->iso_year,b->iso_month,b->iso_day};
    for(int i=0;i<3;i++){ if(af[i]<bf[i])return ts_value_make_int(-1); if(af[i]>bf[i])return ts_value_make_int(1);} return ts_value_make_int(0);
}
TsValue* ts_temporal_plainyearmonth_with_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* pd=require_plainyearmonth(ctx,"with"); validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr; void* raw=arg?ts_nanbox_safe_unbox(arg):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||is_temporal_typed_object(raw)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    int vals[2]={pd->iso_year,pd->iso_month}; static const char* names[2]={"year","month"}; bool any=false;
    { TsValue* f=ts_object_get_property(raw,"year"); if(f&&!ts_value_is_undefined(f)){any=true; double d=ts_to_number(f); if(d!=d||std::isinf(d)){ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite"));return ts_value_make_undefined();} vals[0]=(int)std::trunc(d);} }
    { int bagM=read_bag_month(raw); if(bagM>=1){ any=true; vals[1]=bagM; } }   // month/monthCode + conflict
    if(!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if(vals[1]<1)vals[1]=1; if(vals[1]>12)vals[1]=12;
    return ts_value_make_object(TsPlainYearMonth::Create(vals[0],vals[1],pd->iso_day));
}
TsValue* ts_temporal_plainyearmonth_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_plainyearmonth_from(argc,argv); }
}
extern "C" TsValue* ts_temporal_plainyearmonth_from(int argc, TsValue** argv){
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    auto _ovopt=[&]()->bool{ return validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); };   // overflow read after the input is parsed
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ const char* u=((TsString*)ts_value_get_string(item))->ToUtf8(); int Y,M; if(!u||has_utc_designator(u)||!parse_iso_yearmonth(u,&Y,&M)||M<1||M>12||!iso_date_valid(Y,M,1)||!iso_yearmonth_in_limits(Y,M)||!iso_annotations_valid(u)||!string_calendar_is_iso(u)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth.from: invalid string")); return ts_value_make_undefined(); } _ovopt(); return ts_value_make_object(TsPlainYearMonth::Create(Y,M,1)); }
        if(*(uint32_t*)((char*)raw+16)==TsPlainYearMonth::MAGIC){ TsPlainYearMonth* o=(TsPlainYearMonth*)raw; _ovopt(); return ts_value_make_object(TsPlainYearMonth::Create(o->iso_year,o->iso_month,o->iso_day)); }
        TsValue* fy=ts_object_get_property(raw,"year");
        bool hY=fy&&!ts_value_is_undefined(fy); int bagM=read_bag_month(raw);
        if(!hY||bagM<1){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.from: object needs year and month")); return ts_value_make_undefined(); }
        if(!bag_calendar_ok(raw)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth.from: invalid calendar")); return ts_value_make_undefined(); }
        bool _ovrej = _ovopt();   // overflow read after the bag fields
        double dy=ts_to_number(fy); if(dy!=dy){ ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite")); return ts_value_make_undefined(); }
        int Y=(int)std::trunc(dy),M=bagM;
        if(_ovrej){ if(M<1||M>12){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth.from: month out of range (overflow reject)")); return ts_value_make_undefined(); } }
        else { if(M<1)M=1; if(M>12)M=12; }
        if(!iso_date_valid(Y,M,1)||!iso_yearmonth_in_limits(Y,M)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","out of range")); return ts_value_make_undefined(); }
        return ts_value_make_object(TsPlainYearMonth::Create(Y,M,1));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.from: invalid argument")); return ts_value_make_undefined();
}

// ====================== Temporal.PlainMonthDay ======================
TsPlainMonthDay* TsPlainMonthDay::Create(int m, int d, int refYear) {
    void* mem = ts_alloc(sizeof(TsPlainMonthDay));
    TsPlainMonthDay* o = new (mem) TsPlainMonthDay();
    o->magic = MAGIC; o->iso_month = m; o->iso_day = d; o->iso_year = refYear;
    return o;
}
TsValue TsPlainMonthDay::GetPropertyVirtual(const char* key) {
    auto mkInt=[](long long v){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=v; return r; };
    auto mkStr=[](const char* s){ TsValue r; r.type=ValueType::STRING_PTR; r.ptr_val=TsString::Create(s); return r; };
    TsValue undef; undef.type=ValueType::UNDEFINED; undef.i_val=0;
    if (strcmp(key,"day")==0) return mkInt(iso_day);
    if (strcmp(key,"calendarId")==0) return mkStr("iso8601");
    if (strcmp(key,"monthCode")==0) { char b[8]; snprintf(b,sizeof(b),"M%02d",iso_month); return mkStr(b); }
    return undef;
}
static TsPlainMonthDay* as_plainmonthday(void* raw) {
    if (!raw) return nullptr;
    uint32_t m0=*(uint32_t*)raw;
    if (m0==0x53545247||m0==0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw+16)==TsPlainMonthDay::MAGIC)?(TsPlainMonthDay*)raw:nullptr;
}
extern "C" { void* ts_temporal_get_plainmonthday_ctor(); TsValue* ts_temporal_plainmonthday_from(int argc, TsValue** argv); }
static TsPlainMonthDay* require_plainmonthday(void* ctx, const char* method) {
    if (!ctx) ctx = ts_get_call_this();
    TsPlainMonthDay* d = as_plainmonthday(ts_nanbox_safe_unbox(ctx));
    if (!d) { std::string msg=std::string("Temporal.PlainMonthDay.prototype.")+method+" called on an object that is not a Temporal.PlainMonthDay"; ts_throw((TsValue*)ts_error_create_typed("TypeError",msg.c_str())); }
    return d;
}
extern "C" TsValue* ts_temporal_plainmonthday_construct(int argc, TsValue** argv) {
    auto fld=[&](int i,bool* ok)->int{ if(i>=argc||!argv||!argv[i]||ts_value_is_undefined(argv[i])){*ok=false;return 0;} double d=ts_to_number(argv[i]); if(d!=d||std::isinf(d)){ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay: field not finite"));return 0;} return (int)std::trunc(d); };
    bool om=true,od=true,ory=true; int m=fld(0,&om),d=fld(1,&od);
    validate_iso_calendar_arg((argc>=3&&argv)?argv[2]:nullptr);   // calendar validated after day, before the reference year
    int refY=fld(3,&ory); if(!ory) refY=1972;
    if(!om||!od){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay: month and day are required")); return ts_value_make_undefined(); }
    if(m<1||m>12||d<1||d>iso_days_in_month(refY,m)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay: out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainMonthDay::Create(m,d,refY));
}
static bool parse_iso_monthday(const char* s, int* M, int* D) {
    if (has_unicode_minus(s)) return false;
    const char* p=s; if(p[0]=='-'&&p[1]=='-') p+=2;
    // could be MM-DD or YYYY-MM-DD; if 4+ leading digits treat as date.
    int lead=0; const char* q=p; while(isdigit((unsigned char)*q)){lead++;q++;}
    if(lead>=4){ int yy,mm,dd; if(!parse_iso_date(s,&yy,&mm,&dd)||!date_string_suffix_ok(s)) return false; *M=mm;*D=dd;return true; }
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); p+=2; if(*p=='-')p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int da=(p[0]-'0')*10+(p[1]-'0'); p+=2; *M=mo; *D=da;
    // date-only MM-DD: no UTC offset after DD (time present = a 'T'/'t' before '[').
    { const char* br=strchr(s,'['); size_t dtlen=br?(size_t)(br-s):strlen(s);
      bool hasTime=false; for(size_t i=0;i<dtlen;i++) if(s[i]=='T'||s[i]=='t'||s[i]==' '){ hasTime=true; break; }
      if(!hasTime && *p!=0 && *p!='[') return false; }
    return true;
}
extern "C" {
TsValue* ts_temporal_plainmonthday_toString_native(void* ctx,int argc,TsValue** argv){
    TsPlainMonthDay* d=require_plainmonthday(ctx,"toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    static const char* CALV[]={"auto","always","never","critical"};
    std::string cal = read_enum_option((argc>=1&&argv)?argv[0]:nullptr, "calendarName", "auto", CALV, 4);
    bool showCal = (cal=="always"||cal=="critical");
    const char* ann = (cal=="critical") ? "[!u-ca=iso8601]" : "[u-ca=iso8601]";
    char b[48];
    if(showCal) snprintf(b,sizeof(b),"%04d-%02d-%02d%s",d->iso_year,d->iso_month,d->iso_day,ann);
    else snprintf(b,sizeof(b),"%02d-%02d",d->iso_month,d->iso_day);
    return ts_value_make_string(TsString::Create(b));
}
TsValue* ts_temporal_plainmonthday_valueOf_native(void* ctx,int argc,TsValue** argv){ (void)ctx; ts_throw((TsValue*)ts_error_create_typed("TypeError","Called valueOf on a Temporal.PlainMonthDay; use equals() instead")); return ts_value_make_undefined(); }
TsValue* ts_temporal_plainmonthday_equals_native(void* ctx,int argc,TsValue** argv){
    TsPlainMonthDay* a=require_plainmonthday(ctx,"equals"); TsValue* o=(argc>=1&&argv)?argv[0]:nullptr;
    TsPlainMonthDay* b=as_plainmonthday(o?ts_nanbox_safe_unbox(o):nullptr);
    if(!b){ TsValue* c=ts_temporal_plainmonthday_from(o?1:0,&o); b=as_plainmonthday(ts_nanbox_safe_unbox(c)); if(!b) return ts_value_make_bool(false); }
    return ts_value_make_bool(a->iso_month==b->iso_month&&a->iso_day==b->iso_day&&a->iso_year==b->iso_year);
}
TsValue* ts_temporal_plainmonthday_with_native(void* ctx,int argc,TsValue** argv){
    TsPlainMonthDay* pd=require_plainmonthday(ctx,"with"); validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr; void* raw=arg?ts_nanbox_safe_unbox(arg):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||is_temporal_typed_object(raw)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    int M=pd->iso_month,D=pd->iso_day; bool any=false;
    TsValue* fm=ts_object_get_property(raw,"month"); TsValue* fd=ts_object_get_property(raw,"day");
    if(fm&&!ts_value_is_undefined(fm)){any=true; double d=ts_to_number(fm); if(std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: month property cannot be Infinity")); return ts_value_make_undefined(); } if(d==d)M=(int)std::trunc(d);}
    if(fd&&!ts_value_is_undefined(fd)){any=true; double d=ts_to_number(fd); if(std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: day property cannot be Infinity")); return ts_value_make_undefined(); } if(d==d)D=(int)std::trunc(d);}
    if(!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(pd->iso_year,M); if(D<1)D=1; if(D>dim)D=dim;
    return ts_value_make_object(TsPlainMonthDay::Create(M,D,pd->iso_year));
}
TsValue* ts_temporal_plainmonthday_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_plainmonthday_from(argc,argv); }
}
extern "C" TsValue* ts_temporal_plainmonthday_from(int argc, TsValue** argv){
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    auto _ovopt=[&](){ validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); };   // overflow read after the input is parsed
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ const char* u=((TsString*)ts_value_get_string(item))->ToUtf8(); int M,D; if(!u||has_utc_designator(u)||!parse_iso_monthday(u,&M,&D)||M<1||M>12||D<1||D>iso_days_in_month(1972,M)||!iso_annotations_valid(u)||!string_calendar_is_iso(u)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay.from: invalid string")); return ts_value_make_undefined(); } _ovopt(); return ts_value_make_object(TsPlainMonthDay::Create(M,D,1972)); }
        if(*(uint32_t*)((char*)raw+16)==TsPlainMonthDay::MAGIC){ TsPlainMonthDay* o=(TsPlainMonthDay*)raw; _ovopt(); return ts_value_make_object(TsPlainMonthDay::Create(o->iso_month,o->iso_day,o->iso_year)); }
        TsValue* fd=ts_object_get_property(raw,"day");
        int bagM=read_bag_month(raw); bool hD=fd&&!ts_value_is_undefined(fd);
        if(bagM<1||!hD){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.from: object needs month and day")); return ts_value_make_undefined(); }
        if(!bag_calendar_ok(raw)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay.from: invalid calendar")); return ts_value_make_undefined(); }
        // year is read (and coerced) even though the reference year is 1972.
        TsValue* fy=ts_object_get_property(raw,"year");
        if(fy&&!ts_value_is_undefined(fy)){ double dy=ts_to_number(fy); if(std::isinf(dy)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: year property cannot be Infinity")); return ts_value_make_undefined(); } }
        double dd=ts_to_number(fd); if(dd!=dd||std::isinf(dd)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: day property cannot be Infinity or NaN")); return ts_value_make_undefined(); }
        _ovopt();   // overflow read after the bag fields
        int M=bagM,D=(int)std::trunc(dd); if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(1972,M); if(D<1)D=1; if(D>dim)D=dim;
        return ts_value_make_object(TsPlainMonthDay::Create(M,D,1972));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.from: invalid argument")); return ts_value_make_undefined();
}

// ====================== Temporal.PlainDateTime ======================
TsPlainDateTime* TsPlainDateTime::Create(int y,int mo,int d,int h,int mi,int s,int ms,int us,int ns){
    void* mem=ts_alloc(sizeof(TsPlainDateTime)); TsPlainDateTime* o=new(mem) TsPlainDateTime();
    o->magic=MAGIC; o->iso_year=y; o->iso_month=mo; o->iso_day=d;
    o->iso_hour=h; o->iso_minute=mi; o->iso_second=s; o->iso_ms=ms; o->iso_us=us; o->iso_ns=ns;
    return o;
}
TsValue TsPlainDateTime::GetPropertyVirtual(const char* key){
    auto mkInt=[](long long v){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=v; return r; };
    auto mkBool=[](bool v){ TsValue r; r.type=ValueType::BOOLEAN; r.i_val=v?1:0; return r; };
    auto mkStr=[](const char* s){ TsValue r; r.type=ValueType::STRING_PTR; r.ptr_val=TsString::Create(s); return r; };
    TsValue undef; undef.type=ValueType::UNDEFINED; undef.i_val=0;
    if(strcmp(key,"year")==0) return mkInt(iso_year);
    if(strcmp(key,"month")==0) return mkInt(iso_month);
    if(strcmp(key,"day")==0) return mkInt(iso_day);
    if(strcmp(key,"hour")==0) return mkInt(iso_hour);
    if(strcmp(key,"minute")==0) return mkInt(iso_minute);
    if(strcmp(key,"second")==0) return mkInt(iso_second);
    if(strcmp(key,"millisecond")==0) return mkInt(iso_ms);
    if(strcmp(key,"microsecond")==0) return mkInt(iso_us);
    if(strcmp(key,"nanosecond")==0) return mkInt(iso_ns);
    if(strcmp(key,"calendarId")==0) return mkStr("iso8601");
    if(strcmp(key,"dayOfWeek")==0) return mkInt(iso_day_of_week(iso_year,iso_month,iso_day));
    if(strcmp(key,"dayOfYear")==0) return mkInt(iso_day_of_year(iso_year,iso_month,iso_day));
    if(strcmp(key,"daysInWeek")==0) return mkInt(7);
    if(strcmp(key,"daysInMonth")==0) return mkInt(iso_days_in_month(iso_year,iso_month));
    if(strcmp(key,"daysInYear")==0) return mkInt(iso_is_leap(iso_year)?366:365);
    if(strcmp(key,"monthsInYear")==0) return mkInt(12);
    if(strcmp(key,"inLeapYear")==0) return mkBool(iso_is_leap(iso_year));
    if(strcmp(key,"monthCode")==0){ char b[8]; snprintf(b,sizeof(b),"M%02d",iso_month); return mkStr(b); }
    if(strcmp(key,"weekOfYear")==0||strcmp(key,"yearOfWeek")==0){
        int isoDow=iso_day_of_week(iso_year,iso_month,iso_day); int ordinal=iso_day_of_year(iso_year,iso_month,iso_day);
        int week=(ordinal-isoDow+10)/7; int yow=iso_year;
        if(week<1){yow=iso_year-1;week=iso_weeks_in_year(iso_year-1);} else if(week>iso_weeks_in_year(iso_year)){yow=iso_year+1;week=1;}
        return (key[0]=='w')?mkInt(week):mkInt(yow);
    }
    return undef;  // era/eraYear/other
}
static TsPlainDateTime* as_plaindatetime(void* raw){
    if(!raw) return nullptr; uint32_t m0=*(uint32_t*)raw;
    if(m0==0x53545247||m0==0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw+16)==TsPlainDateTime::MAGIC)?(TsPlainDateTime*)raw:nullptr;
}
extern "C" { void* ts_temporal_get_plaindatetime_ctor(); TsValue* ts_temporal_plaindatetime_from(int argc, TsValue** argv); }
static TsPlainDateTime* require_plaindatetime(void* ctx, const char* method){
    if(!ctx) ctx=ts_get_call_this(); TsPlainDateTime* d=as_plaindatetime(ts_nanbox_safe_unbox(ctx));
    if(!d){ std::string msg=std::string("Temporal.PlainDateTime.prototype.")+method+" called on an object that is not a Temporal.PlainDateTime"; ts_throw((TsValue*)ts_error_create_typed("TypeError",msg.c_str())); }
    return d;
}
static bool pdt_time_valid(int h,int mi,int s,int ms,int us,int ns){
    return h>=0&&h<=23&&mi>=0&&mi<=59&&s>=0&&s<=59&&ms>=0&&ms<=999&&us>=0&&us<=999&&ns>=0&&ns<=999;
}
extern "C" TsValue* ts_temporal_plaindatetime_construct(int argc, TsValue** argv){
    auto fld=[&](int i,int def,bool req,bool* ok)->int{
        if(i>=argc||!argv||!argv[i]||ts_value_is_undefined(argv[i])){ if(req)*ok=false; return def; }
        double d=ts_to_number(argv[i]); if(d!=d||std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime: field not finite")); return def; }
        return (int)std::trunc(d);
    };
    bool ok=true;
    int y=fld(0,0,true,&ok), mo=fld(1,0,true,&ok), d=fld(2,0,true,&ok);
    int h=fld(3,0,false,&ok), mi=fld(4,0,false,&ok), s=fld(5,0,false,&ok);
    int ms=fld(6,0,false,&ok), us=fld(7,0,false,&ok), ns=fld(8,0,false,&ok);
    if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime: year, month and day are required")); return ts_value_make_undefined(); }
    if(!iso_date_valid(y,mo,d)||!pdt_time_valid(h,mi,s,ms,us,ns)||!iso_datetime_in_limits(y,mo,d,(long long)h*3600000000000LL+(long long)mi*60000000000LL+(long long)s*1000000000LL+(long long)ms*1000000LL+(long long)us*1000LL+ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime: out of range")); return ts_value_make_undefined(); }
    validate_iso_calendar_arg((argc>=10&&argv)?argv[9]:nullptr);
    return ts_value_make_object(TsPlainDateTime::Create(y,mo,d,h,mi,s,ms,us,ns));
}
static TsString* plaindatetime_iso_string(TsPlainDateTime* d){
    char buf[40]; int n;
    if(d->iso_year<0||d->iso_year>9999) n=snprintf(buf,sizeof(buf),"%+07d-%02d-%02dT%02d:%02d:%02d",d->iso_year,d->iso_month,d->iso_day,d->iso_hour,d->iso_minute,d->iso_second);
    else n=snprintf(buf,sizeof(buf),"%04d-%02d-%02dT%02d:%02d:%02d",d->iso_year,d->iso_month,d->iso_day,d->iso_hour,d->iso_minute,d->iso_second);
    long frac=(long)d->iso_ms*1000000L+(long)d->iso_us*1000L+d->iso_ns;
    if(frac>0){ char fb[16]; snprintf(fb,sizeof(fb),"%09ld",frac); int len=9; while(len>1&&fb[len-1]=='0')len--; buf[n++]='.'; for(int i=0;i<len;i++)buf[n++]=fb[i]; buf[n]='\0'; }
    return TsString::Create(buf);
}
// Parse "YYYY-MM-DD[T ]HH:MM:SS[.frac]" — date required, time optional.
// After the date/time, only an optional UTC offset (Z or ±HH:MM[:SS[.fff]]) then
// any number of [..] annotations then end-of-string are valid.
static bool iso_datetime_suffix_ok(const char* p){
    if(*p=='Z'||*p=='z'){ p++; }
    else if(*p=='+'||*p=='-'){ p++;
        if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false; p+=2;
        if(*p==':')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])) p+=2;
        if(*p==':')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ p+=2;
            if(*p=='.'||*p==','){ p++; if(!isdigit((unsigned char)*p)) return false; while(isdigit((unsigned char)*p))p++; } }
    }
    while(*p=='['){ const char* e=strchr(p,']'); if(!e) return false; p=e+1; }
    return *p==0;
}
static bool parse_iso_datetime(const char* s,int* Y,int* M,int* D,int* H,int* Mi,int* S,int* ms,int* us,int* ns){
    *H=0;*Mi=0;*S=0;*ms=0;*us=0;*ns=0;
    const char* dend;
    if(!parse_iso_date_e(s,Y,M,D,&dend)) return false;
    if(!iso_date_valid(*Y,*M,*D)) return false;
    // The datetime part ends at the first annotation '[' (avoid scanning a 't' in a
    // name like "Asia/Kolkata"); annotations are validated separately by the caller.
    const char* br=strchr(s,'['); const char* dtEnd = br?br:(s+strlen(s));
    const char* p=dend; bool hasTime=false;
    if(p<dtEnd && (*p=='T'||*p=='t'||*p==' ')){
        hasTime=true; p++;
        auto two=[&](int* o)->bool{ if(p+1>=dtEnd||!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1]))return false; *o=(p[0]-'0')*10+(p[1]-'0'); p+=2; return true; };
        if(!two(H)) return false; if(p<dtEnd&&*p==':')p++;
        if(p+1<dtEnd&&isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ two(Mi); if(p<dtEnd&&*p==':')p++;
            if(p+1<dtEnd&&isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ two(S);
                if(p<dtEnd&&(*p=='.'||*p==',')){ p++; char fb[10]="000000000"; int i=0; while(i<9&&p<dtEnd&&isdigit((unsigned char)*p)){fb[i++]=*p++;} long f=atol(fb); *ms=(int)(f/1000000);*us=(int)((f/1000)%1000);*ns=(int)(f%1000); if(p<dtEnd&&isdigit((unsigned char)*p)) return false; } } }
        if(*S==60)*S=59;
        if(*H>23||*Mi>59||*S>59) return false;
    }
    // Optional UTC offset (Z or ±HH:MM[:SS[.fff]]). An offset requires a time.
    bool hasOffset=false;
    if(p<dtEnd && (*p=='Z'||*p=='z')){ hasOffset=true; p++; }
    else if(p<dtEnd && (*p=='+'||*p=='-')){ hasOffset=true; p++;
        if(p+1>=dtEnd||!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
        if((p[0]-'0')*10+(p[1]-'0')>23) return false; p+=2;                          // offset hour <= 23
        if(p<dtEnd&&*p==':')p++; if(p+1<dtEnd&&isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ if((p[0]-'0')*10+(p[1]-'0')>59) return false; p+=2; }
        if(p<dtEnd&&*p==':')p++; if(p+1<dtEnd&&isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ if((p[0]-'0')*10+(p[1]-'0')>59) return false; p+=2;
            if(p<dtEnd&&(*p=='.'||*p==',')){ p++; if(p>=dtEnd||!isdigit((unsigned char)*p))return false; while(p<dtEnd&&isdigit((unsigned char)*p))p++; } }
    }
    if(hasOffset && !hasTime) return false;   // offset without a time is invalid
    if(p!=dtEnd) return false;                 // trailing junk in the datetime part
    return true;
}
extern "C" {
TsValue* ts_temporal_plaindatetime_toString_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    TsValue* opts=(argc>=1&&argv)?argv[0]:nullptr;
    if(!opts||ts_value_is_undefined(opts)) return ts_value_make_string(plaindatetime_iso_string(d));
    int carry=0; std::string tstr=format_time_opts(d->iso_hour,d->iso_minute,d->iso_second,d->iso_ms,d->iso_us,d->iso_ns,opts,&carry);
    int Y=d->iso_year,M=d->iso_month,D=d->iso_day;
    if(carry){ iso_civil_from_days(iso_days_from_civil(Y,M,D)+carry,&Y,&M,&D); }   // rounding crossed midnight
    char db[24];
    if(Y<0||Y>9999) snprintf(db,sizeof(db),"%+07d-%02d-%02d",Y,M,D);
    else snprintf(db,sizeof(db),"%04d-%02d-%02d",Y,M,D);
    std::string base=db; base+="T"; base+=tstr;
    static const char* CALV[]={"auto","always","never","critical"};
    std::string cal=read_enum_option(opts,"calendarName","auto",CALV,4);
    if(cal=="always"||cal=="critical") base += (cal=="critical")?"[!u-ca=iso8601]":"[u-ca=iso8601]";
    return ts_value_make_string(TsString::Create(base.c_str()));
}
TsValue* ts_temporal_plaindatetime_valueOf_native(void* ctx,int argc,TsValue** argv){ (void)ctx; ts_throw((TsValue*)ts_error_create_typed("TypeError","Called valueOf on a Temporal.PlainDateTime; use compare() or equals() instead")); return ts_value_make_undefined(); }
TsValue* ts_temporal_plaindatetime_equals_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* a=require_plaindatetime(ctx,"equals"); TsValue* o=(argc>=1&&argv)?argv[0]:nullptr;
    TsPlainDateTime* b=as_plaindatetime(o?ts_nanbox_safe_unbox(o):nullptr);
    if(!b){ TsValue* c=ts_temporal_plaindatetime_from(o?1:0,&o); b=as_plaindatetime(ts_nanbox_safe_unbox(c)); if(!b) return ts_value_make_bool(false); }
    return ts_value_make_bool(a->iso_year==b->iso_year&&a->iso_month==b->iso_month&&a->iso_day==b->iso_day&&a->iso_hour==b->iso_hour&&a->iso_minute==b->iso_minute&&a->iso_second==b->iso_second&&a->iso_ms==b->iso_ms&&a->iso_us==b->iso_us&&a->iso_ns==b->iso_ns);
}
TsValue* ts_temporal_plaindatetime_compare_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; auto to=[](TsValue* v)->TsPlainDateTime*{ TsPlainDateTime* p=as_plaindatetime(v?ts_nanbox_safe_unbox(v):nullptr); if(p)return p; TsValue* c=ts_temporal_plaindatetime_from(v?1:0,&v); return as_plaindatetime(ts_nanbox_safe_unbox(c)); };
    TsPlainDateTime* a=to((argc>=1)?argv[0]:nullptr),*b=to((argc>=2)?argv[1]:nullptr); if(!a||!b) return ts_value_make_int(0);
    int af[9]={a->iso_year,a->iso_month,a->iso_day,a->iso_hour,a->iso_minute,a->iso_second,a->iso_ms,a->iso_us,a->iso_ns};
    int bf[9]={b->iso_year,b->iso_month,b->iso_day,b->iso_hour,b->iso_minute,b->iso_second,b->iso_ms,b->iso_us,b->iso_ns};
    for(int i=0;i<9;i++){ if(af[i]<bf[i])return ts_value_make_int(-1); if(af[i]>bf[i])return ts_value_make_int(1);} return ts_value_make_int(0);
}
TsValue* ts_temporal_plaindatetime_with_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* pd=require_plaindatetime(ctx,"with"); bool _ovrej=validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr; void* raw=arg?ts_nanbox_safe_unbox(arg):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||is_temporal_typed_object(raw)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    static const char* names[9]={"year","month","day","hour","minute","second","millisecond","microsecond","nanosecond"};
    int vals[9]={pd->iso_year,pd->iso_month,pd->iso_day,pd->iso_hour,pd->iso_minute,pd->iso_second,pd->iso_ms,pd->iso_us,pd->iso_ns};
    bool any=false;
    for(int i=0;i<9;i++){ if(i==1) continue;   // month handled via read_bag_month (month/monthCode + conflict)
        TsValue* f=ts_object_get_property(raw,names[i]); if(f&&!ts_value_is_undefined(f)){any=true; double d=ts_to_number(f); if(d!=d||std::isinf(d)){ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite"));return ts_value_make_undefined();} vals[i]=(int)std::trunc(d);} }
    { int bagM=read_bag_month(raw); if(bagM>=1){ any=true; vals[1]=bagM; } }
    if(!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if(_ovrej){ const int tl[6]={23,59,59,999,999,999}; bool bad=vals[1]<1||vals[1]>12||vals[2]<1||vals[2]>iso_days_in_month(vals[0],vals[1]); for(int i=0;i<6&&!bad;i++) if(vals[3+i]<0||vals[3+i]>tl[i]) bad=true; if(bad){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.prototype.with: field out of range (overflow reject)")); return ts_value_make_undefined(); } }
    if(vals[1]<1)vals[1]=1; if(vals[1]>12)vals[1]=12; int dim=iso_days_in_month(vals[0],vals[1]); if(vals[2]<1)vals[2]=1; if(vals[2]>dim)vals[2]=dim;
    int* T=vals+3; const int lim[6]={23,59,59,999,999,999}; for(int i=0;i<6;i++){ if(T[i]<0)T[i]=0; if(T[i]>lim[i])T[i]=lim[i]; }
    return ts_value_make_object(TsPlainDateTime::Create(vals[0],vals[1],vals[2],vals[3],vals[4],vals[5],vals[6],vals[7],vals[8]));
}
TsValue* ts_temporal_plaindatetime_toPlainDate_native(void* ctx,int argc,TsValue** argv){ TsPlainDateTime* d=require_plaindatetime(ctx,"toPlainDate"); return ts_value_make_object(TsPlainDate::Create(d->iso_year,d->iso_month,d->iso_day)); }
TsValue* ts_temporal_plaindatetime_toPlainTime_native(void* ctx,int argc,TsValue** argv){ TsPlainDateTime* d=require_plaindatetime(ctx,"toPlainTime"); return ts_value_make_object(TsPlainTime::Create(d->iso_hour,d->iso_minute,d->iso_second,d->iso_ms,d->iso_us,d->iso_ns)); }
TsValue* ts_temporal_plaindatetime_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_plaindatetime_from(argc,argv); }
}
extern "C" TsValue* ts_temporal_plaindatetime_from(int argc, TsValue** argv){
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    auto _ovopt=[&]()->bool{ return validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); };   // overflow read after the input is parsed
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ const char* u=((TsString*)ts_value_get_string(item))->ToUtf8(); int Y,M,D,H,Mi,S,ms,us,ns;
            if(!u||has_utc_designator(u)||!parse_iso_datetime(u,&Y,&M,&D,&H,&Mi,&S,&ms,&us,&ns)||!date_string_suffix_ok(u)||!iso_date_valid(Y,M,D)||!pdt_time_valid(H,Mi,S,ms,us,ns)||!iso_datetime_in_limits(Y,M,D,(long long)H*3600000000000LL+(long long)Mi*60000000000LL+(long long)S*1000000000LL+(long long)ms*1000000LL+(long long)us*1000LL+ns)||!iso_annotations_valid(u)||!string_calendar_is_iso(u)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.from: invalid string")); return ts_value_make_undefined(); }
            _ovopt(); return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,H,Mi,S,ms,us,ns)); }
        if(*(uint32_t*)((char*)raw+16)==TsPlainDateTime::MAGIC){ TsPlainDateTime* o=(TsPlainDateTime*)raw; _ovopt(); return ts_value_make_object(TsPlainDateTime::Create(o->iso_year,o->iso_month,o->iso_day,o->iso_hour,o->iso_minute,o->iso_second,o->iso_ms,o->iso_us,o->iso_ns)); }
        TsValue* fy=ts_object_get_property(raw,"year"),*fd=ts_object_get_property(raw,"day");
        int bagM=read_bag_month(raw);
        bool hY=fy&&!ts_value_is_undefined(fy),hD=fd&&!ts_value_is_undefined(fd);
        if(!hY||bagM<1||!hD){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.from: object needs year, month and day")); return ts_value_make_undefined(); }
        if(!bag_calendar_ok(raw)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.from: invalid calendar")); return ts_value_make_undefined(); }
        auto rd=[&](const char* k,int def)->int{ TsValue* f=ts_object_get_property(raw,k); if(!f||ts_value_is_undefined(f))return def; double d=ts_to_number(f); if(std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: property must be a finite number")); } if(d!=d)return 0; return (int)std::trunc(d); };
        int Y=rd("year",0),M=bagM,D=rd("day",1),H=rd("hour",0),Mi=rd("minute",0),S=rd("second",0),ms=rd("millisecond",0),us=rd("microsecond",0),ns=rd("nanosecond",0);
        bool _ovrej = _ovopt();   // overflow read after the bag fields
        // overflow:"reject" -> any out-of-range field is a RangeError (no clamping).
        if(_ovrej){
            const int tl[6]={23,59,59,999,999,999}; int tv[6]={H,Mi,S,ms,us,ns};
            bool bad = M<1||M>12 || D<1||D>iso_days_in_month(Y,M);
            for(int i=0;i<6&&!bad;i++) if(tv[i]<0||tv[i]>tl[i]) bad=true;
            if(bad){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: field out of range (overflow reject)")); return ts_value_make_undefined(); }
        }
        if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(Y,M); if(D<1)D=1; if(D>dim)D=dim;
        const int lim[6]={23,59,59,999,999,999}; int* tp[6]={&H,&Mi,&S,&ms,&us,&ns}; for(int i=0;i<6;i++){ if(*tp[i]<0)*tp[i]=0; if(*tp[i]>lim[i])*tp[i]=lim[i]; }
        long long tNs=(long long)H*3600000000000LL+(long long)Mi*60000000000LL+(long long)S*1000000000LL+(long long)ms*1000000LL+(long long)us*1000LL+ns;
        if(!iso_date_valid(Y,M,D) || !iso_datetime_in_limits(Y,M,D,tNs)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","out of range")); return ts_value_make_undefined(); }
        return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,H,Mi,S,ms,us,ns));
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.from: invalid argument")); return ts_value_make_undefined();
}

// ============================ Temporal.Now ============================
#include <chrono>
#include <ctime>
static void temporal_now_fields(int* Y,int* M,int* D,int* h,int* m,int* s,int* ms,int* us,int* ns){
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    long long sub = duration_cast<nanoseconds>(now.time_since_epoch()).count() % 1000000000LL;
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &tt);
#else
    gmtime_r(&tt, &tmv);
#endif
    *Y=tmv.tm_year+1900; *M=tmv.tm_mon+1; *D=tmv.tm_mday;
    *h=tmv.tm_hour; *m=tmv.tm_min; *s=tmv.tm_sec;
    *ms=(int)(sub/1000000); *us=(int)((sub/1000)%1000); *ns=(int)(sub%1000);
}
// Temporal.Now.*ISO(timeZoneLike?) — the time-zone argument must be a valid
// identifier even though ts-aot renders fields in UTC.
static void validate_now_tz(int argc, TsValue** argv){
    if(argc>=1&&argv&&argv[0]&&!ts_value_is_undefined(argv[0])){
        std::string tz; int o; bool u;
        if(!tsvalue_to_stdstring(argv[0],&tz)||!parse_timezone(tz.c_str(),&o,&u)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Now: invalid time zone")); }
    }
}
extern "C" {
TsValue* ts_temporal_now_plaindatetimeiso_native(void* ctx,int argc,TsValue** argv){
    validate_now_tz(argc,argv);
    int Y,M,D,h,m,s,ms,us,ns; temporal_now_fields(&Y,&M,&D,&h,&m,&s,&ms,&us,&ns);
    return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,h,m,s,ms,us,ns));
}
TsValue* ts_temporal_now_plaindateiso_native(void* ctx,int argc,TsValue** argv){
    validate_now_tz(argc,argv);
    int Y,M,D,h,m,s,ms,us,ns; temporal_now_fields(&Y,&M,&D,&h,&m,&s,&ms,&us,&ns);
    return ts_value_make_object(TsPlainDate::Create(Y,M,D));
}
TsValue* ts_temporal_now_plaintimeiso_native(void* ctx,int argc,TsValue** argv){
    validate_now_tz(argc,argv);
    int Y,M,D,h,m,s,ms,us,ns; temporal_now_fields(&Y,&M,&D,&h,&m,&s,&ms,&us,&ns);
    return ts_value_make_object(TsPlainTime::Create(h,m,s,ms,us,ns));
}
TsValue* ts_temporal_now_timezoneid_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; return ts_value_make_string(TsString::Create("UTC"));
}
}

// ============================ Temporal.Instant ============================
extern "C" {
    void* ts_bigint_create_int(int64_t val);
    void* ts_bigint_create_str(void* tsStr, int32_t radix);
    void* ts_bigint_to_string(void* bi, int32_t radix);
    TsValue* ts_value_make_bigint(void* b);
}
static void iso_civil_from_days(long long z, int* y, int* m, int* d){
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int yy = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    unsigned mp = (5*doy + 2)/153;
    unsigned dd = doy - (153*mp+2)/5 + 1;
    unsigned mm = mp < 10 ? mp+3 : mp-9;
    *y = yy + (mm <= 2); *m = (int)mm; *d = (int)dd;
}
TsInstant* TsInstant::Create(long long ms, int subNs){
    void* mem=ts_alloc(sizeof(TsInstant)); TsInstant* o=new(mem) TsInstant();
    o->magic=MAGIC; o->epoch_ms=ms; o->sub_ns=subNs; return o;
}
// Format epoch nanoseconds from a (epoch_ms, sub_ns) pair as a signed decimal
// string. Robust to BOTH storage conventions: TRUNC-toward-zero (sub_ns same
// sign as epoch_ms, e.g. from the bigint constructor) and FLOOR (sub_ns in
// [0,999999], e.g. from wall-clock re-encoding via zdt_from_local). Normalizes
// to FLOOR first, then formats with a string-level borrow so a mixed-sign value
// can't overflow int64 (epoch_ms*1e6 would).
static void format_epoch_ns_pair(long long ems, long long sns, char* buf, size_t n){
    if(sns < 0){ ems -= 1; sns += 1000000; }   // TRUNC -> FLOOR (sns in [0,999999])
    if(ems < 0){
        if(sns == 0) snprintf(buf, n, "-%lld000000", -ems);
        else         snprintf(buf, n, "-%lld%06lld", -ems-1, 1000000-sns);
    } else {
        snprintf(buf, n, "%lld%06lld", ems, sns);
    }
}
static void instant_ns_string(TsInstant* it, char* buf, size_t n){
    format_epoch_ns_pair(it->epoch_ms, it->sub_ns, buf, n);
}
TsValue TsInstant::GetPropertyVirtual(const char* key){
    if(strcmp(key,"epochMilliseconds")==0){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=epoch_ms; return r; }
    if(strcmp(key,"epochSeconds")==0){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=epoch_ms/1000; return r; }
    if(strcmp(key,"epochNanoseconds")==0){ char b[40]; instant_ns_string(this,b,sizeof(b)); TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_str((void*)TsString::Create(b),10); return r; }
    if(strcmp(key,"epochMicroseconds")==0){ long long em=epoch_ms,sn=sub_ns; if(sn<0){em-=1;sn+=1000000;} TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_int(em*1000LL + sn/1000); return r; }
    TsValue u; u.type=ValueType::UNDEFINED; u.i_val=0; return u;
}
static TsInstant* as_instant(void* raw){
    if(!raw) return nullptr; uint32_t m0=*(uint32_t*)raw;
    if(m0==0x53545247||m0==0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw+16)==TsInstant::MAGIC)?(TsInstant*)raw:nullptr;
}
extern "C" { void* ts_temporal_get_instant_ctor(); TsValue* ts_temporal_instant_from(int argc, TsValue** argv); }
static TsInstant* require_instant(void* ctx, const char* method){
    if(!ctx) ctx=ts_get_call_this(); TsInstant* d=as_instant(ts_nanbox_safe_unbox(ctx));
    if(!d){ std::string msg=std::string("Temporal.Instant.prototype.")+method+" called on an object that is not a Temporal.Instant"; ts_throw((TsValue*)ts_error_create_typed("TypeError",msg.c_str())); }
    return d;
}
// Decompose a decimal nanoseconds string into truncated ms + sub-ns. Returns
// false if out of the valid Instant range (|ns| <= 8.64e21).
static bool ns_string_to_ms_sub(const char* s, long long* ms, int* sub){
    bool neg=false; const char* p=s; if(*p=='+'||*p=='-'){neg=(*p=='-');p++;}
    int len=0; const char* q=p; while(*q>='0'&&*q<='9'){len++;q++;}
    if(len==0||*q) return false;
    // last 6 digits -> sub_ns, the rest -> ms
    int subDigits = len>=6?6:len;
    long long subv=0; for(int i=len-subDigits;i<len;i++) subv=subv*10+(p[i]-'0');
    // pad if fewer than 6 (small magnitudes): subv is the low digits, ms=0
    for(int i=subDigits;i<6;i++) subv*=10;  // shouldn't happen for len>=6
    long long msv=0; for(int i=0;i<len-6;i++){ msv=msv*10+(p[i]-'0'); if(msv> 9000000000000000LL) return false; }
    if(len<=6){ msv=0; }
    *ms = neg ? -msv : msv;
    *sub = (int)(neg ? -subv : subv);
    if(msv > 8640000000000000LL) return false;  // ~ +/-100M days in ms
    return true;
}
extern "C" TsValue* ts_temporal_instant_construct(int argc, TsValue** argv){
    // new Temporal.Instant(epochNanoseconds: bigint)
    TsValue* a0=(argc>=1&&argv)?argv[0]:nullptr;
    void* raw=a0?ts_nanbox_safe_unbox(a0):nullptr;
    if(!raw || *(uint32_t*)((char*)raw+16)!=0x42494749 /*BigInt at off16*/){
        // also accept bare-bigint magic at offset 0
        if(!raw || *(uint32_t*)raw!=0x42494749){
            ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant: epochNanoseconds must be a BigInt"));
            return ts_value_make_undefined();
        }
    }
    void* str = ts_bigint_to_string(raw, 10);
    const char* u = str ? ((TsString*)str)->ToUtf8() : nullptr;
    long long ms; int sub;
    if(!u || !ns_string_to_ms_sub(u,&ms,&sub) || !instant_epoch_in_limits(ms,sub)){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant: epochNanoseconds out of range"));
        return ts_value_make_undefined();
    }
    return ts_value_make_object(TsInstant::Create(ms,sub));
}
static TsString* instant_iso_string(TsInstant* it){
    // FLOOR sub-second: borrow 1ms so sub_ns lands in [0,999999] for negative epoch.
    long long ms=it->epoch_ms; long long sns=it->sub_ns;
    if(sns<0){ ms-=1; sns+=1000000; }
    long long days = ms / 86400000LL; long long rem = ms % 86400000LL;
    if(rem < 0){ rem += 86400000LL; days -= 1; }
    int Y,M,D; iso_civil_from_days(days,&Y,&M,&D);
    int h=(int)(rem/3600000); rem%=3600000; int mi=(int)(rem/60000); rem%=60000; int s=(int)(rem/1000); int msr=(int)(rem%1000);
    long frac=(long)msr*1000000L + (long)sns; // ns within second (floored)
    char buf[48]; int n;
    if(Y<0||Y>9999) n=snprintf(buf,sizeof(buf),"%+07d-%02d-%02dT%02d:%02d:%02d",Y,M,D,h,mi,s);
    else n=snprintf(buf,sizeof(buf),"%04d-%02d-%02dT%02d:%02d:%02d",Y,M,D,h,mi,s);
    if(frac>0){ char fb[16]; snprintf(fb,sizeof(fb),"%09ld",frac); int len=9; while(len>1&&fb[len-1]=='0')len--; buf[n++]='.'; for(int i=0;i<len;i++)buf[n++]=fb[i]; }
    buf[n++]='Z'; buf[n]='\0';
    return TsString::Create(buf);
}
extern "C" {
TsValue* ts_temporal_instant_epochNs_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"epochNanoseconds"); char b[40]; instant_ns_string(it,b,sizeof(b));
    void* bi=ts_bigint_create_str((void*)TsString::Create(b),10); return ts_value_make_bigint(bi);
}
TsValue* ts_temporal_instant_epochMicros_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"epochMicroseconds"); long long micros=it->epoch_ms*1000LL + it->sub_ns/1000;
    return ts_value_make_bigint(ts_bigint_create_int(micros));
}
TsValue* ts_temporal_instant_toString_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    TsValue* opts=(argc>=1&&argv)?argv[0]:nullptr;
    if(!opts||ts_value_is_undefined(opts)) return ts_value_make_string(instant_iso_string(it));
    // timeZone-rendered output is unsupported here -> fall back to default UTC string.
    void* raw=ts_nanbox_safe_unbox(opts);
    if(raw){ TsValue* tz=ts_object_get_property(raw,"timeZone"); if(tz&&!ts_value_is_undefined(tz)){
        // The timeZone must still be a valid identifier (ToTemporalTimeZoneIdentifier)
        // even though ts-aot renders the default UTC string for it.
        std::string tzs; int o; bool u;
        if(!tsvalue_to_stdstring(tz,&tzs)||!parse_timezone(tzs.c_str(),&o,&u)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.toString: invalid time zone")); return ts_value_make_undefined(); }
        return ts_value_make_string(instant_iso_string(it)); } }
    long long ms=it->epoch_ms; long long sns=it->sub_ns;
    if(sns<0){ ms-=1; sns+=1000000; }   // FLOOR sub-second for negative epoch
    long long days=ms/86400000LL; long long rem=ms%86400000LL;
    if(rem<0){ rem+=86400000LL; days-=1; }
    int h=(int)(rem/3600000); rem%=3600000; int mi=(int)(rem/60000); rem%=60000; int s=(int)(rem/1000); int msr=(int)(rem%1000);
    int us=(int)(sns/1000), ns=(int)(sns%1000);
    int carry=0; std::string ts=format_time_opts(h,mi,s,msr,us,ns,opts,&carry);   // rounding may cross midnight
    int Y,M,D; iso_civil_from_days(days+carry,&Y,&M,&D);
    char db[24]; if(Y<0||Y>9999) snprintf(db,sizeof(db),"%+07d-%02d-%02d",Y,M,D); else snprintf(db,sizeof(db),"%04d-%02d-%02d",Y,M,D);
    std::string out=db; out+="T"; out+=ts; out+="Z";
    return ts_value_make_string(TsString::Create(out.c_str()));
}
TsValue* ts_temporal_instant_valueOf_native(void* ctx,int argc,TsValue** argv){ (void)ctx; ts_throw((TsValue*)ts_error_create_typed("TypeError","Called valueOf on a Temporal.Instant; use compare() or equals() instead")); return ts_value_make_undefined(); }
TsValue* ts_temporal_instant_equals_native(void* ctx,int argc,TsValue** argv){
    TsInstant* a=require_instant(ctx,"equals"); TsValue* o=(argc>=1&&argv)?argv[0]:nullptr;
    TsInstant* b=as_instant(o?ts_nanbox_safe_unbox(o):nullptr);
    if(!b){ TsValue* c=ts_temporal_instant_from(o?1:0,&o); b=as_instant(ts_nanbox_safe_unbox(c)); if(!b) return ts_value_make_bool(false); }
    return ts_value_make_bool(a->epoch_ms==b->epoch_ms && a->sub_ns==b->sub_ns);
}
TsValue* ts_temporal_instant_compare_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; auto to=[](TsValue* v)->TsInstant*{ TsInstant* p=as_instant(v?ts_nanbox_safe_unbox(v):nullptr); if(p)return p; TsValue* c=ts_temporal_instant_from(v?1:0,&v); return as_instant(ts_nanbox_safe_unbox(c)); };
    TsInstant* a=to((argc>=1)?argv[0]:nullptr),*b=to((argc>=2)?argv[1]:nullptr); if(!a||!b) return ts_value_make_int(0);
    if(a->epoch_ms!=b->epoch_ms) return ts_value_make_int(a->epoch_ms<b->epoch_ms?-1:1);
    if(a->sub_ns!=b->sub_ns) return ts_value_make_int(a->sub_ns<b->sub_ns?-1:1);
    return ts_value_make_int(0);
}
TsValue* ts_temporal_instant_fromEpochMs_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; if(argc>=1&&argv&&argv[0]) reject_nonnumeric_increment(argv[0]);  // ToNumber(BigInt) -> TypeError
    double d=(argc>=1&&argv&&argv[0])?ts_to_number(argv[0]):0; if(d!=d||std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fromEpochMilliseconds: not finite")); return ts_value_make_undefined(); }
    if(d!=std::trunc(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.fromEpochMilliseconds: must be an integer")); return ts_value_make_undefined(); }  // NumberToBigInt
    long long ems=(long long)d;
    if(!instant_epoch_in_limits(ems,0)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.fromEpochMilliseconds: epoch is out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create(ems,0));
}
TsValue* ts_temporal_instant_fromEpochSec_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; if(argc>=1&&argv&&argv[0]) reject_nonnumeric_increment(argv[0]);  // ToNumber(BigInt) -> TypeError
    double d=(argc>=1&&argv&&argv[0])?ts_to_number(argv[0]):0; if(d!=d||std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fromEpochSeconds: not finite")); return ts_value_make_undefined(); }
    if(d!=std::trunc(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.fromEpochSeconds: must be an integer")); return ts_value_make_undefined(); }
    if(d>8640000000000.0 || d<-8640000000000.0){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.fromEpochSeconds: epoch is out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create((long long)d*1000LL,0));
}
TsValue* ts_temporal_instant_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_instant_from(argc,argv); }
}
// Extract a trailing UTC offset (Z, or +/-HH[:MM][:SS]) from an Instant string.
// Returns the offset in milliseconds (Z -> 0); *found=false if there is none.
// Parse the UTC offset of an ISO datetime string into whole milliseconds, and the
// SUB-millisecond part (seconds' fractional nanoseconds) into *offSubNs. The offset
// VALUE may carry sub-minute precision (±HH:MM:SS.fffffffff).
static long long parse_instant_offset_ms(const char* s, bool* found, long long* offSubNs){
    *found=false; *offSubNs=0;
    const char* p=s; while(*p && *p!='T' && *p!='t' && *p!=' ') p++;
    if(!*p) return 0;
    p++;
    while(*p && *p!='Z' && *p!='z' && *p!='+' && *p!='-' && *p!='[') p++;
    if(*p=='Z'||*p=='z'){ *found=true; return 0; }
    if(*p=='+'||*p=='-'){
        int sign=(*p=='-')?-1:1; p++;
        if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return 0;
        long long oh=(p[0]-'0')*10+(p[1]-'0'); p+=2;
        long long om=0,os=0,frac=0;
        if(*p==':')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ om=(p[0]-'0')*10+(p[1]-'0'); p+=2; }
        if(*p==':')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ os=(p[0]-'0')*10+(p[1]-'0'); p+=2; }
        if(*p=='.'||*p==','){ p++; long long mult=100000000LL; while(isdigit((unsigned char)*p)){ frac+=(*p-'0')*mult; mult/=10; p++; } }
        *found=true; *offSubNs = sign*frac;
        return sign*(oh*3600000LL + om*60000LL + os*1000LL);
    }
    return 0;
}
// |epochNanoseconds| <= 8.64e21 ns  <=>  epoch_ms within +/-8.64e15 (sub-ns ignored
// at the loose boundary). Returns false if clearly out of the representable range.
static bool instant_ms_in_range(long long epoch_ms){
    return epoch_ms <= 8640000000000000LL && epoch_ms >= -8640000000000000LL;
}
extern "C" TsValue* ts_temporal_instant_from(int argc, TsValue** argv){
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        if(*(uint32_t*)((char*)raw+16)==TsInstant::MAGIC){ TsInstant* o=(TsInstant*)raw; return ts_value_make_object(TsInstant::Create(o->epoch_ms,o->sub_ns)); }
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){
            const char* u=((TsString*)ts_value_get_string(item))->ToUtf8();
            // Parse "YYYY-MM-DDTHH:MM:SS[.frac](Z|+/-HH:MM)" -> epoch.
            int Y,M,D,h,mi,s,ms,us,ns;
            bool hasOff=false; long long offSubNs=0; long long offMs=parse_instant_offset_ms(u,&hasOff,&offSubNs);
            if(!u || !parse_iso_datetime(u,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns)||!iso_annotations_valid(u)||!hasOff){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.from: invalid string")); return ts_value_make_undefined(); }
            long long days=iso_days_from_civil(Y,M,D);
            long long epoch_ms = days*86400000LL + (long long)h*3600000LL + (long long)mi*60000LL + (long long)s*1000LL + ms - offMs;
            long long subNs = (long long)us*1000 + ns - offSubNs;
            while(subNs<0){ subNs+=1000000; epoch_ms-=1; } while(subNs>=1000000){ subNs-=1000000; epoch_ms+=1; }
            if(!instant_ms_in_range(epoch_ms) || (epoch_ms==8640000000000000LL && subNs>0)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.from: instant out of range")); return ts_value_make_undefined(); }
            return ts_value_make_object(TsInstant::Create(epoch_ms, (int)subNs));
        }
    }
    // Not an Instant or a string: ToString the argument (an object's toString is
    // invoked observably; a symbol throws TypeError) and parse it as an Instant.
    {
        std::string str;
        if(!option_to_string(item,&str)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.from: invalid argument")); return ts_value_make_undefined(); }
        const char* u=str.c_str(); int Y,M,D,h,mi,s,ms,us,ns;
        bool hasOff=false; long long offSubNs=0; long long offMs=parse_instant_offset_ms(u,&hasOff,&offSubNs);
        if(!parse_iso_datetime(u,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns)||!iso_annotations_valid(u)||!hasOff){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.from: invalid string")); return ts_value_make_undefined(); }
        long long days=iso_days_from_civil(Y,M,D);
        long long epoch_ms=days*86400000LL+(long long)h*3600000LL+(long long)mi*60000LL+(long long)s*1000LL+ms - offMs;
        long long subNs=(long long)us*1000+ns - offSubNs;
        while(subNs<0){ subNs+=1000000; epoch_ms-=1; } while(subNs>=1000000){ subNs-=1000000; epoch_ms+=1; }
        if(!instant_ms_in_range(epoch_ms) || (epoch_ms==8640000000000000LL && subNs>0)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.from: instant out of range")); return ts_value_make_undefined(); }
        return ts_value_make_object(TsInstant::Create(epoch_ms, (int)subNs));
    }
}

// ====================== Temporal.ZonedDateTime ======================
TsZonedDateTime* TsZonedDateTime::Create(long long ms, int subNs, int offMin, bool utc){
    void* mem=ts_alloc(sizeof(TsZonedDateTime)); TsZonedDateTime* o=new(mem) TsZonedDateTime();
    o->magic=MAGIC; o->epoch_ms=ms; o->sub_ns=subNs; o->offset_minutes=offMin; o->is_utc=utc; return o;
}
// Local wall-clock breakdown (epoch + fixed offset).
static void zdt_local(TsZonedDateTime* z,int* Y,int* M,int* D,int* h,int* mi,int* s,int* ms,int* us,int* ns){
    // Wall-clock fields are FLOOR-based: the sub-second components must land in
    // [0,999999] even for a negative epoch. Storage may be TRUNC-toward-zero
    // (epoch_ms and sub_ns both negative), so first borrow 1ms to bring sub_ns
    // into [0,999999], then decompose with floored arithmetic.
    long long ems = z->epoch_ms; long long sns = z->sub_ns;
    if(sns < 0){ ems -= 1; sns += 1000000; }
    long long local = ems + (long long)z->offset_minutes*60000LL;
    long long days = local/86400000LL; long long rem = local%86400000LL;
    if(rem<0){ rem+=86400000LL; days-=1; }
    iso_civil_from_days(days,Y,M,D);
    *h=(int)(rem/3600000); rem%=3600000; *mi=(int)(rem/60000); rem%=60000; *s=(int)(rem/1000); *ms=(int)(rem%1000);
    *us=(int)(sns/1000); *ns=(int)(sns%1000);
}
static void zdt_offset_string(int offMin, char* buf, size_t n){
    char sign = offMin<0?'-':'+'; int a=offMin<0?-offMin:offMin;
    snprintf(buf,n,"%c%02d:%02d",sign,a/60,a%60);
}
TsValue TsZonedDateTime::GetPropertyVirtual(const char* key){
    auto mkInt=[](long long v){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=v; return r; };
    auto mkBool=[](bool v){ TsValue r; r.type=ValueType::BOOLEAN; r.i_val=v?1:0; return r; };
    auto mkStr=[](const char* s){ TsValue r; r.type=ValueType::STRING_PTR; r.ptr_val=TsString::Create(s); return r; };
    TsValue undef; undef.type=ValueType::UNDEFINED; undef.i_val=0;
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(this,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    if(strcmp(key,"year")==0) return mkInt(Y);
    if(strcmp(key,"month")==0) return mkInt(M);
    if(strcmp(key,"day")==0) return mkInt(D);
    if(strcmp(key,"hour")==0) return mkInt(h);
    if(strcmp(key,"minute")==0) return mkInt(mi);
    if(strcmp(key,"second")==0) return mkInt(s);
    if(strcmp(key,"millisecond")==0) return mkInt(ms);
    if(strcmp(key,"microsecond")==0) return mkInt(us);
    if(strcmp(key,"nanosecond")==0) return mkInt(ns);
    if(strcmp(key,"calendarId")==0) return mkStr("iso8601");
    if(strcmp(key,"dayOfWeek")==0) return mkInt(iso_day_of_week(Y,M,D));
    if(strcmp(key,"dayOfYear")==0) return mkInt(iso_day_of_year(Y,M,D));
    if(strcmp(key,"daysInWeek")==0) return mkInt(7);
    if(strcmp(key,"daysInMonth")==0) return mkInt(iso_days_in_month(Y,M));
    if(strcmp(key,"daysInYear")==0) return mkInt(iso_is_leap(Y)?366:365);
    if(strcmp(key,"monthsInYear")==0) return mkInt(12);
    if(strcmp(key,"inLeapYear")==0) return mkBool(iso_is_leap(Y));
    if(strcmp(key,"hoursInDay")==0) return mkInt(24);
    if(strcmp(key,"monthCode")==0){ char b[8]; snprintf(b,sizeof(b),"M%02d",M); return mkStr(b); }
    if(strcmp(key,"epochMilliseconds")==0) return mkInt(epoch_ms);
    if(strcmp(key,"epochSeconds")==0) return mkInt(epoch_ms/1000);
    if(strcmp(key,"offsetNanoseconds")==0) return mkInt((long long)offset_minutes*60000000000LL);
    if(strcmp(key,"timeZoneId")==0){ if(is_utc) return mkStr("UTC"); char tb[8]; zdt_offset_string(offset_minutes,tb,sizeof(tb)); return mkStr(tb); }
    if(strcmp(key,"offset")==0){ char b[8]; zdt_offset_string(offset_minutes,b,sizeof(b)); return mkStr(b); }
    if(strcmp(key,"weekOfYear")==0||strcmp(key,"yearOfWeek")==0){
        int isoDow=iso_day_of_week(Y,M,D); int ordinal=iso_day_of_year(Y,M,D);
        int week=(ordinal-isoDow+10)/7; int yow=Y;
        if(week<1){yow=Y-1;week=iso_weeks_in_year(Y-1);} else if(week>iso_weeks_in_year(Y)){yow=Y+1;week=1;}
        return (key[0]=='w')?mkInt(week):mkInt(yow);
    }
    if(strcmp(key,"epochNanoseconds")==0){ char b[40]; format_epoch_ns_pair(epoch_ms,sub_ns,b,sizeof(b)); TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_str((void*)TsString::Create(b),10); return r; }
    if(strcmp(key,"epochMicroseconds")==0){ long long em=epoch_ms,sn=sub_ns; if(sn<0){em-=1;sn+=1000000;} TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_int(em*1000LL+sn/1000); return r; }
    return undef;
}
static TsZonedDateTime* as_zoneddatetime(void* raw){
    if(!raw) return nullptr; uint32_t m0=*(uint32_t*)raw;
    if(m0==0x53545247||m0==0x434F4E53) return nullptr;
    return (*(uint32_t*)((char*)raw+16)==TsZonedDateTime::MAGIC)?(TsZonedDateTime*)raw:nullptr;
}
extern "C" { void* ts_temporal_get_zoneddatetime_ctor(); }
static TsZonedDateTime* require_zoneddatetime(void* ctx, const char* method){
    if(!ctx) ctx=ts_get_call_this(); TsZonedDateTime* d=as_zoneddatetime(ts_nanbox_safe_unbox(ctx));
    if(!d){ std::string msg=std::string("Temporal.ZonedDateTime.prototype.")+method+" called on an object that is not a Temporal.ZonedDateTime"; ts_throw((TsValue*)ts_error_create_typed("TypeError",msg.c_str())); }
    return d;
}
// Parse a time-zone string: "UTC" or a numeric offset "+HH:MM"/"-HH:MM"/"+HHMM"/Z.
// A ZonedDateTime property-bag "offset" field is a UTC offset VALUE (not an
// identifier), so it requires a sign and allows sub-minute precision
// (±HH[:MM[:SS[.fff]]]). "00:00" (no sign) and junk are invalid.
static bool valid_offset_field(const char* s){
    if(!s) return false;
    const char* p=s;
    if(*p!='+'&&*p!='-') return false; p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    if((p[0]-'0')*10+(p[1]-'0')>23) return false; p+=2;
    if(*p==0) return true;
    if(*p==':')p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    if((p[0]-'0')*10+(p[1]-'0')>59) return false; p+=2;
    if(*p==0) return true;
    if(*p==':')p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false; p+=2;   // seconds
    if(*p==0) return true;
    if(*p=='.'||*p==','){ p++; if(!isdigit((unsigned char)*p)) return false; while(isdigit((unsigned char)*p))p++; }
    return *p==0;
}
// Parse a (format-valid) offset field into total nanoseconds (sign * (h:m:s.fff)).
static long long offset_field_ns(const char* s){
    int sign=(s[0]=='-')?-1:1; const char* p=s+1;
    long long hh=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    long long mm=0,ss=0,frac=0;
    if(*p==':')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ mm=(p[0]-'0')*10+(p[1]-'0'); p+=2; }
    if(*p==':')p++; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ ss=(p[0]-'0')*10+(p[1]-'0'); p+=2; }
    if(*p=='.'||*p==','){ p++; long long mult=100000000LL; while(isdigit((unsigned char)*p)){ frac+=(*p-'0')*mult; mult/=10; p++; } }
    return sign*((hh*3600+mm*60+ss)*1000000000LL + frac);
}
static bool parse_timezone(const char* s, int* offMin, bool* isUtc){
    if(!s) return false;
    // A datetime-form time-zone string ("YYYYY-MM-DDThh:mm±..." or with Z) must be a
    // valid ISO datetime: reject a Unicode minus anywhere, and a negative-zero
    // extended year (which only appears in the datetime, not a bare ±HH:MM offset).
    if(has_unicode_minus(s)) return false;
    if(strchr(s,'T') && has_negative_zero_year(s)) return false;
    if(strcmp(s,"UTC")==0||strcmp(s,"utc")==0){ *offMin=0; *isUtc=true; return true; }
    // Datetime/bracketed time-zone string: the identifier is the [...] annotation
    // (the bracket "wins" over an inline offset, e.g. "...T17:30+01:00[-08:00]").
    { const char* lb=strchr(s,'[');
      if(lb){ const char* rb=strchr(lb,']'); if(!rb) return false;
          std::string ann(lb+1,(size_t)(rb-lb-1));
          if(!ann.empty()&&ann[0]=='!') ann=ann.substr(1);
          if(ann.compare(0,5,"u-ca=")==0) return false;   // calendar-only annotation: no tz
          return parse_timezone(ann.c_str(), offMin, isUtc); } }
    const char* p=s; if(*p=='Z'||*p=='z'){ *offMin=0; *isUtc=true; return true; }
    int sign=0; if(*p=='+')sign=1; else if(*p=='-')sign=-1; else return false; p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int hh=(p[0]-'0')*10+(p[1]-'0'); p+=2; if(*p==':')p++;
    int mm=0; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])){ mm=(p[0]-'0')*10+(p[1]-'0'); p+=2; }
    // An offset time-zone IDENTIFIER is minute precision only (±HH:MM); a trailing
    // ":SS" / ".fff" sub-minute component (or any junk) makes it invalid.
    if(*p!=0) return false;
    *offMin=sign*(hh*60+mm); *isUtc=false; return true;
}
extern "C" TsValue* ts_temporal_zoneddatetime_construct(int argc, TsValue** argv){
    // new Temporal.ZonedDateTime(epochNanoseconds: bigint, timeZone: string)
    TsValue* a0=(argc>=1&&argv)?argv[0]:nullptr; void* raw0=a0?ts_nanbox_safe_unbox(a0):nullptr;
    bool isBig = raw0 && (*(uint32_t*)raw0==0x42494749 || *(uint32_t*)((char*)raw0+16)==0x42494749);
    if(!isBig){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime: epochNanoseconds must be a BigInt")); return ts_value_make_undefined(); }
    void* str=ts_bigint_to_string(raw0,10); const char* u=str?((TsString*)str)->ToUtf8():nullptr;
    long long ms; int sub; if(!u||!ns_string_to_ms_sub(u,&ms,&sub)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime: epochNanoseconds out of range")); return ts_value_make_undefined(); }
    TsValue* a1=(argc>=2&&argv)?argv[1]:nullptr; void* raw1=a1?ts_nanbox_safe_unbox(a1):nullptr;
    if(!raw1 || (*(uint32_t*)raw1!=0x53545247 && *(uint32_t*)raw1!=0x434F4E53)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime: timeZone must be a string")); return ts_value_make_undefined(); }
    const char* tz=((TsString*)ts_value_get_string(a1))->ToUtf8(); int off; bool utc;
    // The constructor timeZone is a bare identifier (offset / "UTC" / name); a
    // bracketed datetime-form string is only valid in the parse-from-string paths.
    if(tz && strchr(tz,'[')){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime: time zone must be a bare identifier")); return ts_value_make_undefined(); }
    if(!parse_timezone(tz,&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime: unsupported time zone (only UTC and numeric offsets)")); return ts_value_make_undefined(); }
    validate_iso_calendar_arg((argc>=3&&argv)?argv[2]:nullptr);
    return ts_value_make_object(TsZonedDateTime::Create(ms,sub,off,utc));
}
// Extract the time-zone annotation (the first bracket group that is not u-ca).
static bool zdt_extract_tz(const char* s, int* offMin, bool* utc){
    const char* lb=strchr(s,'[');
    while(lb){ const char* rb=strchr(lb,']'); if(!rb) return false;
        std::string ann(lb+1,(size_t)(rb-lb-1));
        if(!ann.empty() && ann[0]=='!') ann=ann.substr(1);
        if(ann.compare(0,5,"u-ca=")!=0){ return parse_timezone(ann.c_str(),offMin,utc); }
        lb=strchr(rb+1,'[');
    }
    return false;
}
extern "C" TsValue* ts_temporal_zdt_from(int argc, TsValue** argv){
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    bool _ovrej=false; std::string offMode="reject"; bool _optsRead=false;
    // disambiguation/offset/overflow are read only after the input has been parsed
    // (an invalid string throws before any option is observed).
    auto _readopts=[&](){
        if(_optsRead) return; _optsRead=true;
        TsValue* o=(argc>=2&&argv)?argv[1]:nullptr;
        static const char* DISV[]={"compatible","earlier","later","reject"};
        static const char* OFFFV[]={"prefer","use","ignore","reject"};
        read_enum_option(o,"disambiguation","compatible",DISV,4);
        offMode = read_enum_option(o,"offset","reject",OFFFV,4);
        _ovrej = validate_overflow_option(o);
    };
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item); if(!raw){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.from: invalid argument")); return ts_value_make_undefined(); }
    uint32_t m0=*(uint32_t*)raw;
    if(m0!=0x53545247 && m0!=0x434F4E53){
        _readopts();   // object/bag input: options observed up front (unchanged order)
        if(*(uint32_t*)((char*)raw+16)==TsZonedDateTime::MAGIC){ TsZonedDateTime* z=(TsZonedDateTime*)raw; return ts_value_make_object(TsZonedDateTime::Create(z->epoch_ms,z->sub_ns,z->offset_minutes,z->is_utc)); }
        // property bag: year/month/day + timeZone required.
        TsValue* tzf=ts_object_get_property(raw,"timeZone");
        if(!tzf||ts_value_is_undefined(tzf)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.from: object needs a timeZone")); return ts_value_make_undefined(); }
        void* tzr=ts_nanbox_safe_unbox(tzf); int off; bool utc;
        if(tzr&&(*(uint32_t*)tzr==0x53545247||*(uint32_t*)tzr==0x434F4E53)){ const char* tu=((TsString*)ts_value_get_string(tzf))->ToUtf8(); if(!tu||!parse_timezone(tu,&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: unsupported time zone")); return ts_value_make_undefined(); } }
        else { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: unsupported time zone")); return ts_value_make_undefined(); }
        { TsValue* offf=ts_object_get_property(raw,"offset");
          if(offf && !ts_value_is_undefined(offf)){ std::string os;
              if(!tsvalue_to_stdstring(offf,&os) || !valid_offset_field(os.c_str())){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: invalid offset string")); return ts_value_make_undefined(); }
              // offset:"reject" -> the supplied offset must match the (offset-only)
              // time zone's offset exactly.
              if(offMode=="reject" && offset_field_ns(os.c_str()) != (long long)off*60000000000LL){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: offset does not match the time zone")); return ts_value_make_undefined(); } } }
        int bagM=read_bag_month(raw);
        TsValue* fy=ts_object_get_property(raw,"year"),*fd=ts_object_get_property(raw,"day");
        if(!fy||ts_value_is_undefined(fy)||bagM<1||!fd||ts_value_is_undefined(fd)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.from: object needs year, month and day")); return ts_value_make_undefined(); }
        if(!bag_calendar_ok(raw)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: invalid calendar")); return ts_value_make_undefined(); }
        auto rd=[&](const char* k,int def)->int{ TsValue* f=ts_object_get_property(raw,k); if(!f||ts_value_is_undefined(f))return def; double d=ts_to_number(f); if(std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: property must be a finite number")); } if(d!=d)return 0; return (int)std::trunc(d); };
        int Y=rd("year",0),M=bagM,D=rd("day",1),H=rd("hour",0),Mi=rd("minute",0),S=rd("second",0),ms=rd("millisecond",0),us=rd("microsecond",0),ns=rd("nanosecond",0);
        // overflow:"reject" -> any out-of-range field is a RangeError (no clamping).
        if(_ovrej){
            const int tl[6]={23,59,59,999,999,999}; int tv[6]={H,Mi,S,ms,us,ns};
            bool bad = M<1||M>12 || D<1||D>iso_days_in_month(Y,M);
            for(int i=0;i<6&&!bad;i++) if(tv[i]<0||tv[i]>tl[i]) bad=true;
            if(bad){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: field out of range (overflow reject)")); return ts_value_make_undefined(); }
        }
        if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(Y,M); if(D<1)D=1; if(D>dim)D=dim;
        const int lim[6]={23,59,59,999,999,999}; int* tp[6]={&H,&Mi,&S,&ms,&us,&ns}; for(int i=0;i<6;i++){ if(*tp[i]<0)*tp[i]=0; if(*tp[i]>lim[i])*tp[i]=lim[i]; }
        long long localMs=iso_days_from_civil(Y,M,D)*86400000LL+(long long)H*3600000+(long long)Mi*60000+(long long)S*1000+ms;
        return ts_value_make_object(TsZonedDateTime::Create(localMs-(long long)off*60000LL, us*1000+ns, off, utc));
    }
    // string: "YYYY-MM-DDTHH:MM:SS[.frac]±HH:MM[tz]"
    const char* u=((TsString*)ts_value_get_string(item))->ToUtf8();
    int Y,M,D,H,Mi,S,ms,us,ns;
    if(!u||!parse_iso_datetime(u,&Y,&M,&D,&H,&Mi,&S,&ms,&us,&ns)||!iso_date_valid(Y,M,D)||!pdt_time_valid(H,Mi,S,ms,us,ns)||!iso_annotations_valid(u)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: invalid string")); return ts_value_make_undefined(); }
    int off; bool utc;
    if(!zdt_extract_tz(u,&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: string needs a time zone annotation")); return ts_value_make_undefined(); }
    _readopts();   // options read only after the string is fully parsed
    long long localMs=iso_days_from_civil(Y,M,D)*86400000LL+(long long)H*3600000+(long long)Mi*60000+(long long)S*1000+ms;
    long long epoch_ms=localMs-(long long)off*60000LL;
    if(!instant_epoch_in_limits(epoch_ms, us*1000+ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.from: instant is outside the representable range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(epoch_ms, us*1000+ns, off, utc));
}
extern "C" TsValue* ts_temporal_zdt_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_zdt_from(argc,argv); }
static TsString* zdt_iso_string(TsZonedDateTime* z){
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    char buf[80]; int n;
    if(Y<0||Y>9999) n=snprintf(buf,sizeof(buf),"%+07d-%02d-%02dT%02d:%02d:%02d",Y,M,D,h,mi,s);
    else n=snprintf(buf,sizeof(buf),"%04d-%02d-%02dT%02d:%02d:%02d",Y,M,D,h,mi,s);
    long frac=(long)ms*1000000L+(long)us*1000L+ns;
    if(frac>0){ char fb[16]; snprintf(fb,sizeof(fb),"%09ld",frac); int len=9; while(len>1&&fb[len-1]=='0')len--; buf[n++]='.'; for(int i=0;i<len;i++)buf[n++]=fb[i]; }
    char ob[8]; zdt_offset_string(z->offset_minutes,ob,sizeof(ob)); for(int i=0;ob[i];i++)buf[n++]=ob[i];
    buf[n++]='['; const char* id=z->is_utc?"UTC":ob; for(int i=0;id[i];i++)buf[n++]=id[i]; buf[n++]=']'; buf[n]='\0';
    return TsString::Create(buf);
}
extern "C" {
static TsZonedDateTime* coerce_zdt_arg(TsValue* v);  // defined later in this block
TsValue* ts_temporal_zdt_epochNs_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"epochNanoseconds"); TsValue v=z->GetPropertyVirtual("epochNanoseconds"); return (TsValue*)v.ptr_val; }
TsValue* ts_temporal_zdt_epochMicros_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"epochMicroseconds"); TsValue v=z->GetPropertyVirtual("epochMicroseconds"); return (TsValue*)v.ptr_val; }
TsValue* ts_temporal_zdt_toString_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"toString");
    require_options_object((argc>=1&&argv)?argv[0]:nullptr);
    TsValue* opts=(argc>=1&&argv)?argv[0]:nullptr;
    if(!opts||ts_value_is_undefined(opts)) return ts_value_make_string(zdt_iso_string(z));
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    int carry=0; std::string tstr=format_time_opts(h,mi,s,ms,us,ns,opts,&carry);   // rounding may cross midnight
    if(carry){ iso_civil_from_days(iso_days_from_civil(Y,M,D)+carry,&Y,&M,&D); }
    char db[24]; if(Y<0||Y>9999) snprintf(db,sizeof(db),"%+07d-%02d-%02d",Y,M,D); else snprintf(db,sizeof(db),"%04d-%02d-%02d",Y,M,D);
    std::string out=db; out+="T"; out+=tstr;
    char ob[8]; zdt_offset_string(z->offset_minutes,ob,sizeof(ob));
    static const char* OFFV[]={"auto","never"};
    std::string offMode=read_enum_option(opts,"offset","auto",OFFV,2);
    if(offMode!="never") out+=ob;
    static const char* TZNV[]={"auto","never","critical"};
    std::string tzn=read_enum_option(opts,"timeZoneName","auto",TZNV,3);
    if(tzn!="never"){ out+="["; out+= z->is_utc?"UTC":ob; out+="]"; }
    static const char* CALV[]={"auto","always","never","critical"};
    std::string cal=read_enum_option(opts,"calendarName","auto",CALV,4);
    if(cal=="always"||cal=="critical") out += (cal=="critical")?"[!u-ca=iso8601]":"[u-ca=iso8601]";
    return ts_value_make_string(TsString::Create(out.c_str()));
}
TsValue* ts_temporal_zdt_valueOf_native(void* ctx,int argc,TsValue** argv){ (void)ctx; ts_throw((TsValue*)ts_error_create_typed("TypeError","Called valueOf on a Temporal.ZonedDateTime; use compare() or equals() instead")); return ts_value_make_undefined(); }
TsValue* ts_temporal_zdt_equals_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* a=require_zoneddatetime(ctx,"equals"); TsValue* o=(argc>=1&&argv)?argv[0]:nullptr;
    TsZonedDateTime* b=coerce_zdt_arg(o); if(!b) return ts_value_make_bool(false);
    // Normalize both to FLOOR so a TRUNC-stored and a FLOOR-stored (re-encoded)
    // instant that denote the same epoch compare equal.
    long long aem=a->epoch_ms,asn=a->sub_ns; if(asn<0){aem-=1;asn+=1000000;}
    long long bem=b->epoch_ms,bsn=b->sub_ns; if(bsn<0){bem-=1;bsn+=1000000;}
    return ts_value_make_bool(aem==bem&&asn==bsn&&a->offset_minutes==b->offset_minutes&&a->is_utc==b->is_utc);
}
TsValue* ts_temporal_zdt_compare_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; TsZonedDateTime* a=coerce_zdt_arg((argc>=1&&argv)?argv[0]:nullptr);
    TsZonedDateTime* b=coerce_zdt_arg((argc>=2&&argv)?argv[1]:nullptr);
    if(!a||!b) return ts_value_make_int(0);
    long long aem=a->epoch_ms,asn=a->sub_ns; if(asn<0){aem-=1;asn+=1000000;}
    long long bem=b->epoch_ms,bsn=b->sub_ns; if(bsn<0){bem-=1;bsn+=1000000;}
    if(aem!=bem) return ts_value_make_int(aem<bem?-1:1);
    if(asn!=bsn) return ts_value_make_int(asn<bsn?-1:1);
    return ts_value_make_int(0);
}
TsValue* ts_temporal_zdt_toInstant_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"toInstant"); return ts_value_make_object(TsInstant::Create(z->epoch_ms,z->sub_ns)); }
TsValue* ts_temporal_zdt_toPlainDateTime_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"toPlainDateTime"); int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns); return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,h,mi,s,ms,us,ns)); }
TsValue* ts_temporal_zdt_toPlainDate_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"toPlainDate"); int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns); return ts_value_make_object(TsPlainDate::Create(Y,M,D)); }
TsValue* ts_temporal_zdt_toPlainTime_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"toPlainTime"); int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns); return ts_value_make_object(TsPlainTime::Create(h,mi,s,ms,us,ns)); }
}

// ======================= Arithmetic: PlainTime =======================
static long long pt_to_ns(TsPlainTime* p){
    return ((((long long)p->iso_hour*60 + p->iso_minute)*60 + p->iso_second)*1000000000LL)
        + (long long)p->iso_millisecond*1000000LL + (long long)p->iso_microsecond*1000LL + p->iso_nanosecond;
}
static TsValue* pt_from_ns(long long ns){
    int h=(int)(ns/3600000000000LL); ns%=3600000000000LL;
    int mi=(int)(ns/60000000000LL); ns%=60000000000LL;
    int s=(int)(ns/1000000000LL); ns%=1000000000LL;
    int ms=(int)(ns/1000000LL); ns%=1000000LL;
    int us=(int)(ns/1000LL); int nn=(int)(ns%1000LL);
    return ts_value_make_object(TsPlainTime::Create(h,mi,s,ms,us,nn));
}
static TsDuration* coerce_duration_arg(TsValue* v){
    TsDuration* d = as_duration(v?ts_nanbox_safe_unbox(v):nullptr);
    if(d) return d;
    TsValue* c = ts_temporal_duration_from(v?1:0,&v);
    return as_duration(ts_nanbox_safe_unbox(c));
}
// ToRelativeTemporalObject (validation half): a present relativeTo must be a valid
// Temporal date / date-time / zoned string (or a date-bearing object). An invalid one
// throws even when the comparison/rounding itself needs no calendar anchoring — the
// observable parse happens regardless. Routes strings through the strict from() parsers.
static void validate_relativeto_arg(TsValue* rt){
    if(!rt||ts_value_is_undefined(rt)) return;
    void* rr=ts_nanbox_safe_unbox(rt);
    if(!rr){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal: relativeTo must be a string or object")); return; }
    uint32_t m0=*(uint32_t*)rr;
    if(m0==0x53545247||m0==0x434F4E53){
        void* sp=ts_value_get_string(rt); const char* ru=sp?((TsString*)sp)->ToUtf8():nullptr;
        // A ZonedDateTime relativeTo needs a UTC designator or a *time-zone* bracket — a
        // calendar-only annotation ([u-ca=...]) is NOT a time zone and stays a PlainDate.
        bool zdtLike=false;
        if(ru){
            if(strchr(ru,'Z')||strchr(ru,'z')) zdtLike=true;
            else { const char* br=strchr(ru,'['); if(br && strncmp(br,"[u-ca=",6)!=0 && strncmp(br,"[!u-ca=",7)!=0) zdtLike=true; }
        }
        if(zdtLike) (void)ts_temporal_zdt_from(1,&rt);   // validates offset/tz/Z, throws on invalid
        else        (void)ts_temporal_plaindate_from(1,&rt);
        return;
    }
    // A Temporal-typed object carries its slot — accept it.
    if(is_temporal_typed_object(rr)) return;
    // Property bag: ToRelativeTemporalObject reads & validates every recognized field.
    // A non-finite (Infinity) numeric field is a RangeError (ToIntegerWithTruncation).
    static const char* NF[]={"year","month","day","hour","minute","second","millisecond","microsecond","nanosecond"};
    for(const char* k : NF){ TsValue* f=ts_object_get_property(rr,k);
        if(f&&!ts_value_is_undefined(f)){ double dv=ts_to_number(f); if(std::isinf(dv)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: relativeTo field cannot be Infinity")); return; } } }
    // An offset field, if present, must be a valid UTC-offset string.
    TsValue* offf=ts_object_get_property(rr,"offset");
    if(offf&&!ts_value_is_undefined(offf)){ std::string os;
        if(!tsvalue_to_stdstring(offf,&os)||!valid_offset_field(os.c_str())){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: relativeTo has an invalid offset")); return; } }
    // A timeZone field, if present, must be a parseable time-zone string.
    TsValue* tzf=ts_object_get_property(rr,"timeZone");
    if(tzf&&!ts_value_is_undefined(tzf)){ void* tr=ts_nanbox_safe_unbox(tzf);
        if(!tr||(*(uint32_t*)tr!=0x53545247&&*(uint32_t*)tr!=0x434F4E53)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal: relativeTo timeZone must be a string")); return; }
        const char* tu=((TsString*)ts_value_get_string(tzf))->ToUtf8(); int off; bool utc;
        if(!tu||!parse_timezone(tu,&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: relativeTo has an invalid time zone")); return; } }
}
// Temporal.Duration.compare(one, two[, options]). Returns -1/0/1. A calendar unit
// (years/months/weeks) in either operand requires options.relativeTo (RangeError
// otherwise); the calendar-anchored comparison is not yet implemented, so this
// handles the time/day case (day = 24h) which is the spec result when no calendar
// units are present.
extern "C" TsValue* ts_temporal_duration_compare_native(void* ctx, int argc, TsValue** argv){
    (void)ctx;
    TsDuration* a = coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    TsDuration* b = coerce_duration_arg((argc>=2&&argv)?argv[1]:nullptr);
    if(!a||!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.compare: invalid argument")); return ts_value_make_undefined(); }
    TsValue* opts=(argc>=3&&argv)?argv[2]:nullptr;
    require_options_object(opts);
    bool hasRel=false;
    if(opts && !ts_value_is_undefined(opts)){ void* r=ts_nanbox_safe_unbox(opts); if(r){ TsValue* rt=ts_object_get_property(r,"relativeTo"); if(rt&&!ts_value_is_undefined(rt)){ hasRel=true; validate_relativeto_arg(rt); } } }
    bool cal = a->years||a->months||a->weeks||b->years||b->months||b->weeks;
    if(cal && !hasRel){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.compare: a calendar unit requires relativeTo")); return ts_value_make_undefined(); }
    auto norm=[](TsDuration* d, long long* sec, long long* ns){
        long long s = d->days*86400LL + d->hours*3600LL + d->minutes*60LL + d->seconds;
        long long n = 0;
        s += d->milliseconds/1000;      n += (d->milliseconds%1000)*1000000LL;
        s += d->microseconds/1000000;   n += (d->microseconds%1000000)*1000LL;
        s += d->nanoseconds/1000000000; n += d->nanoseconds%1000000000;
        s += n/1000000000; n %= 1000000000;
        *sec=s; *ns=n;
    };
    long long sa,na,sb,nb; norm(a,&sa,&na); norm(b,&sb,&nb);
    int r = (sa<sb)?-1:(sa>sb)?1:((na<nb)?-1:(na>nb)?1:0);
    return ts_value_make_int(r);
}
// Add a duration's time components to a time (mod 24h). Each component reduced
// mod its day-cycle so the sum stays within int64.
static long long add_time_ns(long long base, TsDuration* d, int sign){
    const long long DAY=86400000000000LL;
    long long ns = base;
    ns += sign * (d->hours % 24) * 3600000000000LL;
    ns += sign * (d->minutes % 1440) * 60000000000LL;
    ns += sign * (d->seconds % 86400) * 1000000000LL;
    ns += sign * (d->milliseconds % 86400000LL) * 1000000LL;
    ns += sign * (d->microseconds % 86400000000LL) * 1000LL;
    ns += sign * (d->nanoseconds % DAY);
    ns = ((ns % DAY) + DAY) % DAY;
    return ns;
}
static TsValue* duration_from_time_ns(long long diff){
    int sign = diff<0?-1:1; long long ad = diff<0?-diff:diff;
    long long h=ad/3600000000000LL; ad%=3600000000000LL;
    long long mi=ad/60000000000LL; ad%=60000000000LL;
    long long s=ad/1000000000LL; ad%=1000000000LL;
    long long ms=ad/1000000LL; ad%=1000000LL;
    long long us=ad/1000LL; long long nn=ad%1000LL;
    return ts_value_make_object(TsDuration::Create(0,0,0,0, sign*h, sign*mi, sign*s, sign*ms, sign*us, sign*nn));
}
static TsPlainTime* coerce_plaintime_arg(TsValue* v){
    TsPlainTime* p = as_plaintime(v?ts_nanbox_safe_unbox(v):nullptr);
    if(p) return p;
    TsValue* c = ts_temporal_plaintime_from(v?1:0,&v);
    return as_plaintime(ts_nanbox_safe_unbox(c));
}
extern "C" {
TsValue* ts_temporal_plaintime_add_native(void* ctx,int argc,TsValue** argv){
    TsPlainTime* pt=require_plaintime(ctx,"add"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainTime.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    return pt_from_ns(add_time_ns(pt_to_ns(pt),d,1));
}
TsValue* ts_temporal_plaintime_subtract_native(void* ctx,int argc,TsValue** argv){
    TsPlainTime* pt=require_plaintime(ctx,"subtract"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainTime.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    return pt_from_ns(add_time_ns(pt_to_ns(pt),d,-1));
}
TsValue* ts_temporal_plaintime_until_native(void* ctx,int argc,TsValue** argv){
    TsPlainTime* a=require_plaintime(ctx,"until"); TsPlainTime* b=coerce_plaintime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainTime.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return time_diff_with_opts(pt_to_ns(b) - pt_to_ns(a), (argc>=2&&argv)?argv[1]:nullptr, "hour");
}
TsValue* ts_temporal_plaintime_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainTime* a=require_plaintime(ctx,"since"); TsPlainTime* b=coerce_plaintime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainTime.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return time_diff_with_opts(pt_to_ns(a) - pt_to_ns(b), (argc>=2&&argv)?argv[1]:nullptr, "hour");
}
}

// ======================= Arithmetic: PlainDate =======================
static void add_iso_date(int y,int m,int d, long long years,long long months,long long weeks,long long days,
                         int* Y,int* M,int* D){
    long long ym = (long long)(m-1) + months;           // 0-based month index
    long long yy = (long long)y + years + (ym>=0 ? ym/12 : (ym-11)/12);
    int mm = (int)(ym - (yy-(long long)y-years)*12) + 1; // 1-based month after balance
    if(mm<1){mm+=12;yy--;} if(mm>12){mm-=12;yy++;}
    int yi=(int)yy;
    int dim=iso_days_in_month(yi,mm); int dd=d; if(dd>dim) dd=dim; if(dd<1) dd=1;
    long long civil = iso_days_from_civil(yi,mm,dd) + days + weeks*7;
    iso_civil_from_days(civil, Y, M, D);
}
static std::string read_string_option(TsValue* opts, const char* key, const char* def){
    // ECMA-262 GetOptionsObject: undefined -> defaults; an object -> use it;
    // anything else (a primitive: string/number/boolean/null) -> TypeError.
    if(!opts || ts_value_is_undefined(opts)) return def;
    void* raw = ts_nanbox_safe_unbox(opts);
    if(!raw){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); return def; }
    uint32_t m0=*(uint32_t*)raw;
    // string (STRG/CONS), symbol (SYMB), bigint (BIGI) are primitive wrappers, not objects.
    if(m0==0x53545247||m0==0x434F4E53||m0==0x53594D42||m0==0x42494749){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); return def; }
    TsValue* v = ts_object_get_property(raw, key);
    std::string s;
    // option_to_string performs the spec ToString (observable for object values,
    // TypeError for symbol); this is the single observable read for the diff path.
    if(v && !ts_value_is_undefined(v) && option_to_string(v,&s)){ if(s=="auto") return def; return s; }
    return def;
}
// Read a string option that must be one of a fixed enum (calendarName/
// timeZoneName/offset for toString); ToString-coerces (observable) then throws
// RangeError if the value is not allowed.
static std::string read_enum_option(TsValue* opts, const char* key, const char* def, const char* const* valid, int nvalid){
    std::string s = read_string_option(opts, key, def);
    for(int i=0;i<nvalid;i++) if(s==valid[i]) return s;
    char m[80]; snprintf(m,sizeof(m),"invalid %s value", key);
    ts_throw((TsValue*)ts_error_create_typed("RangeError",m));
    return def;
}
static bool temporal_mode_valid(const std::string& m){
    return m=="ceil"||m=="floor"||m=="expand"||m=="trunc"||m=="halfCeil"
         ||m=="halfFloor"||m=="halfExpand"||m=="halfTrunc"||m=="halfEven";
}
extern "C" void* ts_string_from_value(TsValue* val);
extern "C" TsValue* ts_function_call_with_this(TsValue* boxedFunc, TsValue* thisArg, int argc, TsValue** argv);
// ECMAScript ToString for a string-typed option value (GetOption type String):
//   string -> itself; number/boolean/bigint/null -> their primitive string;
//   symbol -> TypeError; object -> ToPrimitive(string): call toString then valueOf
//   (observable), then ToString the primitive result. Returns false (caller skips)
//   only for undefined; otherwise fills *out and may throw. Rejects wrong-typed
//   option values with the right error and invokes an object's toString in spec
//   order (the test262 wrong-type / order-of-operations probes assert this).
static bool option_to_string(TsValue* v, std::string* out){
    if(!v || ts_value_is_undefined(v)) return false;
    void* raw = ts_nanbox_safe_unbox(v);
    if(raw){
        uint32_t m0 = *(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ void* sp=ts_value_get_string(v); if(sp) ((TsString*)sp)->AppendUtf8(*out); return true; }
        if(m0!=0x53594D42 && m0!=0x42494749){  // a non-symbol, non-bigint heap object
            const char* keys[2]={"toString","valueOf"};
            for(int k=0;k<2;k++){
                TsValue* fn = ts_object_get_property(raw, keys[k]);
                if(fn && !ts_value_is_undefined(fn) && ts_is_callable((void*)fn)){
                    TsValue* res = ts_function_call_with_this(fn, v, 0, nullptr);
                    if(res && !ts_value_is_undefined(res)){
                        void* rraw = ts_nanbox_safe_unbox(res);
                        bool resObj = rraw && (*(uint32_t*)rraw!=0x53545247 && *(uint32_t*)rraw!=0x434F4E53 && *(uint32_t*)rraw!=0x53594D42);
                        if(!resObj){ void* s=ts_string_from_value(res); if(s){ ((TsString*)s)->AppendUtf8(*out); return true; } }
                    }
                }
            }
        }
    }
    void* s = ts_string_from_value(v);  // primitive string / TypeError for Symbol / "[object Object]"
    if(!s) return false;
    ((TsString*)s)->AppendUtf8(*out);
    return true;
}
// True if v is a heap OBJECT (not a string/symbol/bigint/number/boolean/null
// primitive) — i.e. ToString would run user code (toString/valueOf). The diff
// path validates options in two passes (validate then compute-read); to invoke
// an object's toString exactly ONCE (the observers assert call count), the
// validate pass skips objects and lets the single compute read_string_option
// (which uses option_to_string) do the observable coercion.
static bool option_is_object(TsValue* v){
    uint64_t nb = nanbox_from_tsvalue_ptr(v);
    if(!nanbox_is_ptr(nb)) return false;
    void* p = nanbox_to_ptr(nb); if(!p) return false;
    uint32_t m = *(uint32_t*)p;
    return !(m==0x53545247||m==0x434F4E53||m==0x53594D42||m==0x42494749);
}
// Coarseness rank: nanosecond=1 .. year=10 (0 = not a temporal unit).
static int unit_rank(const std::string& u){
    if(u=="nanosecond"||u=="nanoseconds")return 1;
    if(u=="microsecond"||u=="microseconds")return 2;
    if(u=="millisecond"||u=="milliseconds")return 3;
    if(u=="second"||u=="seconds")return 4;
    if(u=="minute"||u=="minutes")return 5;
    if(u=="hour"||u=="hours")return 6;
    if(u=="day"||u=="days")return 7;
    if(u=="week"||u=="weeks")return 8;
    if(u=="month"||u=="months")return 9;
    if(u=="year"||u=="years")return 10;
    return 0;
}
// A unit string is allowed for a method when its rank is within [minRank,maxRank].
static bool unit_in_range(const std::string& u, int minRank, int maxRank){
    int r = unit_rank(u);
    return r>=minRank && r<=maxRank;
}
// Validate the shared rounding/diff options. minRank/maxRank bound which units
// the calling method accepts (e.g. Instant.until: hour..nanosecond = [1,6]).
static void validate_round_diff_opts(TsValue* opts, int minRank, int maxRank){
    if(!opts || ts_value_is_undefined(opts)) return;
    void* raw = ts_nanbox_safe_unbox(opts);
    if(!raw){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); return; }
    uint32_t m0=*(uint32_t*)raw;
    // string (STRG/CONS), symbol (SYMB), bigint (BIGI) are primitive wrappers, not objects.
    if(m0==0x53545247||m0==0x434F4E53||m0==0x53594D42||m0==0x42494749){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); return; }
    // roundingMode: validate only when it is a string (avoid false TypeError on
    // ToString-coercible values); an invalid string value is a RangeError.
    // For object values, defer the (observable) ToString to the single compute
    // read (read_string_option); validate only primitives here so an object's
    // toString runs exactly once. Symbol/number/null primitives still throw.
    TsValue* rm = ts_object_get_property(raw,"roundingMode");
    if(rm && !ts_value_is_undefined(rm) && !option_is_object(rm)){
        std::string s;
        if(option_to_string(rm,&s) && !temporal_mode_valid(s)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingMode")); return; }
    }
    // smallestUnit: must be a unit in range.
    std::string suStr, luStr;
    TsValue* su = ts_object_get_property(raw,"smallestUnit");
    if(su && !ts_value_is_undefined(su) && !option_is_object(su)){
        if(option_to_string(su,&suStr) && !unit_in_range(suStr,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid smallestUnit")); return; }
    }
    // largestUnit (also accepts "auto").
    TsValue* lu = ts_object_get_property(raw,"largestUnit");
    if(lu && !ts_value_is_undefined(lu) && !option_is_object(lu)){
        if(option_to_string(lu,&luStr) && luStr!="auto" && !unit_in_range(luStr,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid largestUnit")); return; }
    }
    // largestUnit must be coarser than or equal to smallestUnit (higher/equal rank).
    if(!suStr.empty() && !luStr.empty() && luStr!="auto" && unit_rank(luStr) < unit_rank(suStr)){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","largestUnit must not be smaller than smallestUnit")); return;
    }
    // roundingIncrement: ToNumber; must be finite, then truncate(value) in [1, 1e9].
    // Non-integers are truncated (2.5 -> 2), not rejected; 0.9 -> 0 -> RangeError.
    TsValue* ri = ts_object_get_property(raw,"roundingIncrement");
    if(ri && !ts_value_is_undefined(ri)){
        double dv = (reject_nonnumeric_increment(ri), ts_to_number(ri));
        if(!(dv==dv) || std::isinf(dv)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingIncrement")); return; }
        double ii = std::trunc(dv);
        if(ii<1.0 || ii>1e9){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingIncrement")); return; }
    }
}
// Single observable pass for the diff/round options (smallestUnit, largestUnit,
// roundingMode, roundingIncrement): read each EXACTLY ONCE via read_string_option
// (which ToStrings object values observably — the wrong-type/order observers
// assert the call count), then validate. Replaces the old validate-then-reread
// pattern which invoked an option object's toString twice.
static void require_options_object(TsValue* opts);
// Read an option as a string WITHOUT mapping "auto" to the default — the caller
// validates "auto" where it is not an allowed value (roundingMode, smallestUnit)
// and treats it as the "not provided" sentinel for largestUnit. Observable
// ToString for object values; TypeError for a symbol.
static std::string read_opt_str_noauto(void* raw, const char* key, const char* def){
    if(!raw) return def;
    TsValue* v=ts_object_get_property(raw,key);
    std::string s;
    if(v && !ts_value_is_undefined(v) && option_to_string(v,&s)) return s;
    return def;
}
static void read_validated_diff_opts(TsValue* opts, int minRank, int maxRank,
        const char* defSmallest, const char* defLargest,
        std::string* smallest, std::string* largest, std::string* mode, long long* inc){
    (void)defLargest;  // largestUnit defaults to the "auto" sentinel; the caller resolves it
    require_options_object(opts);  // TypeError for a primitive options arg (safe, no deref)
    void* raw0 = opts?ts_nanbox_safe_unbox(opts):nullptr;
    *smallest = read_opt_str_noauto(raw0, "smallestUnit", defSmallest);
    *largest  = read_opt_str_noauto(raw0, "largestUnit", "auto");
    *mode     = read_opt_str_noauto(raw0, "roundingMode", "trunc");
    *inc = 1;
    void* raw = opts?ts_nanbox_safe_unbox(opts):nullptr;
    if(raw){
        TsValue* ri=ts_object_get_property(raw,"roundingIncrement");
        if(ri&&!ts_value_is_undefined(ri)){
            double dd=(reject_nonnumeric_increment(ri), ts_to_number(ri));
            if(!(dd==dd)||std::isinf(dd)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingIncrement")); return; }
            double ii=std::trunc(dd);
            if(ii<1.0||ii>1e9){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingIncrement")); return; }
            *inc=(long long)ii;
        }
    }
    if(!temporal_mode_valid(*mode)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingMode")); return; }
    if(!unit_in_range(*smallest,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid smallestUnit")); return; }
    if(*largest!="auto" && !unit_in_range(*largest,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid largestUnit")); return; }
    if(*largest!="auto" && unit_rank(*largest) < unit_rank(*smallest)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","largestUnit must not be smaller than smallestUnit")); return; }
}
// GetOptionsObject (object-type check only): throw TypeError for any primitive
// options value. Uses nanbox tag math directly so a primitive (null/bool/number/
// string/symbol/bigint) is never dereferenced as an object (which can corrupt the
// heap). undefined and real objects pass.
static void require_options_object(TsValue* opts){
    if(!opts || ts_value_is_undefined(opts)) return;
    uint64_t nb=(uint64_t)(uintptr_t)opts;
    bool isObj = ((nb & 0xFFFF000000000000ULL)==0) && (nb >= 0x10000);
    if(isObj){
        uint32_t m0=*(uint32_t*)(void*)nb;
        if(m0==0x53545247||m0==0x434F4E53||m0==0x53594D42||m0==0x42494749) isObj=false;
    }
    if(!isObj){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); }
}
// GetOptionsObject + validate the "overflow" option (constrain|reject) for
// add/subtract/with/from. Throws TypeError for a primitive options value and
// RangeError for an out-of-range overflow string (string values only).
// Validate the overflow option and return true iff it is "reject" (read once, so
// a toString observer fires exactly once).
static bool validate_overflow_option(TsValue* opts){
    if(!opts || ts_value_is_undefined(opts)) return false;
    void* raw = ts_nanbox_safe_unbox(opts);
    if(!raw){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); return false; }
    uint32_t m0=*(uint32_t*)raw;
    if(m0==0x53545247||m0==0x434F4E53||m0==0x53594D42||m0==0x42494749){ ts_throw((TsValue*)ts_error_create_typed("TypeError","options must be an object or undefined")); return false; }
    static const char* OV[2]={"constrain","reject"};
    std::string s = read_enum_option(opts,"overflow","constrain",OV,2);
    return s=="reject";
}
// Calendar difference from (ay/am/ad) to (by/bm/bd) per largestUnit.
static void diff_iso_date(int ay,int am,int ad,int by,int bm,int bd, const std::string& largest,
                          long long* yr,long long* mo,long long* wk,long long* dy){
    *yr=*mo=*wk=*dy=0;
    long long totalDays = iso_days_from_civil(by,bm,bd) - iso_days_from_civil(ay,am,ad);
    if(largest=="day"||largest=="days"){ *dy=totalDays; return; }
    if(largest=="week"||largest=="weeks"){ *wk=totalDays/7; *dy=totalDays%7; return; }
    if(totalDays==0) return;
    int sign = totalDays<0?-1:1;
    int sy=ay,sm=am,sd=ad, ey=by,em=bm,ed=bd;
    if(sign<0){ sy=by;sm=bm;sd=bd; ey=ay;em=am;ed=ad; }
    int years = ey - sy, months = em - sm, dd = ed - sd;
    if(dd<0){ months -= 1; int pm=em-1, py=ey; if(pm<1){pm=12;py--;} dd += iso_days_in_month(py,pm); }
    if(months<0){ years -= 1; months += 12; }
    if(largest=="month"||largest=="months"){ months += years*12; years=0; }
    *yr=sign*years; *mo=sign*months; *wk=0; *dy=sign*dd;
}
static TsPlainDate* coerce_plaindate_arg(TsValue* v){
    void* raw = v?ts_nanbox_safe_unbox(v):nullptr;
    TsPlainDate* p = as_plaindate(raw);
    if(p) return p;
    // A ZonedDateTime / PlainDateTime supplies its date from its internal slot —
    // no observable property reads (year/month/day/calendar getters must NOT fire).
    if(raw && (uintptr_t)raw>=4096 && (uintptr_t)raw<=0x00007FFFFFFFFFFFULL){
        uint32_t m16=*(uint32_t*)((char*)raw+16);
        if(m16==0x5A44544D){ TsZonedDateTime* z=(TsZonedDateTime*)raw; int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns); return (TsPlainDate*)TsPlainDate::Create(Y,M,D); }
        if(m16==0x50444D54){ TsPlainDateTime* dt=(TsPlainDateTime*)raw; return (TsPlainDate*)TsPlainDate::Create(dt->iso_year,dt->iso_month,dt->iso_day); }
    }
    TsValue* c = ts_temporal_plaindate_from(v?1:0,&v);
    return as_plaindate(ts_nanbox_safe_unbox(c));
}
extern "C" {
TsValue* ts_temporal_plaindate_add_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"add"); bool _ovrej=validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    // overflow:reject — the day must fit the target year/month (after adding years/
    // months, before the day/week shift), else RangeError instead of clamping.
    if(_ovrej){ long long ty=pd->iso_year+d->years, tm=pd->iso_month+d->months; while(tm>12){tm-=12;ty++;} while(tm<1){tm+=12;ty--;}
        if(pd->iso_day > iso_days_in_month((int)ty,(int)tm)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.add: date does not exist with overflow reject")); return ts_value_make_undefined(); } }
    // Time units balance to whole days (truncating) for a date-only result.
    long long _tns=(long long)d->hours*3600000000000LL+(long long)d->minutes*60000000000LL+(long long)d->seconds*1000000000LL+(long long)d->milliseconds*1000000LL+(long long)d->microseconds*1000LL+d->nanoseconds;
    long long _xd=_tns/86400000000000LL;
    int Y,M,D; add_iso_date(pd->iso_year,pd->iso_month,pd->iso_day, d->years,d->months,d->weeks,d->days+_xd,&Y,&M,&D);
    if(!iso_date_valid(Y,M,D)||!iso_date_in_limits(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.add: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(Y,M,D));
}
TsValue* ts_temporal_plaindate_subtract_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"subtract"); bool _ovrej=validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    if(_ovrej){ long long ty=pd->iso_year-d->years, tm=pd->iso_month-d->months; while(tm>12){tm-=12;ty++;} while(tm<1){tm+=12;ty--;}
        if(pd->iso_day > iso_days_in_month((int)ty,(int)tm)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.subtract: date does not exist with overflow reject")); return ts_value_make_undefined(); } }
    long long _tns=(long long)d->hours*3600000000000LL+(long long)d->minutes*60000000000LL+(long long)d->seconds*1000000000LL+(long long)d->milliseconds*1000000LL+(long long)d->microseconds*1000LL+d->nanoseconds;
    long long _xd=_tns/86400000000000LL;
    int Y,M,D; add_iso_date(pd->iso_year,pd->iso_month,pd->iso_day, -d->years,-d->months,-d->weeks,-d->days-_xd,&Y,&M,&D);
    if(!iso_date_valid(Y,M,D)||!iso_date_in_limits(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.subtract: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(Y,M,D));
}
// Shared option reader for PlainDate until/since (largest/smallest/inc/mode).
static void read_date_diff_opts(TsValue* opts, std::string* smallest, std::string* largest, long long* inc, std::string* mode){
    *smallest=read_string_option(opts,"smallestUnit","day");
    *largest=read_string_option(opts,"largestUnit","auto");
    *mode=read_string_option(opts,"roundingMode","trunc");
    *inc=1;
    void* raw=opts?ts_nanbox_safe_unbox(opts):nullptr;
    if(raw){ TsValue* ri=ts_object_get_property(raw,"roundingIncrement"); if(ri&&!ts_value_is_undefined(ri)){ double dd=(reject_nonnumeric_increment(ri), ts_to_number(ri)); if(dd==dd&&!std::isinf(dd))*inc=(long long)std::trunc(dd); } }
    if(*largest=="auto") *largest = (date_unit_rank(*smallest)>date_unit_rank("day")) ? *smallest : std::string("day");
}
static TsValue* plaindate_diff(int aY,int aM,int aD,int bY,int bM,int bD,TsValue* opts){
    std::string smallest,largest,mode; long long inc;
    read_validated_diff_opts(opts,7,10,"day","auto",&smallest,&largest,&mode,&inc);
    if(largest=="auto") largest = (date_unit_rank(smallest)>date_unit_rank("day")) ? smallest : std::string("day");
    long long yr,mo,wk,dy;
    if((smallest=="day"||smallest=="days") && mode=="trunc" && inc<=1)
        diff_iso_date(aY,aM,aD,bY,bM,bD,largest,&yr,&mo,&wk,&dy);
    else {
        bool _re=false; round_date_duration(aY,aM,aD,bY,bM,bD,smallest,largest,inc,mode,&yr,&mo,&wk,&dy,&_re);
        if(_re){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: rounded date is outside the valid ISO range")); return ts_value_make_undefined(); }
    }
    return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy,0,0,0,0,0,0));
}
TsValue* ts_temporal_plaindate_until_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* a=require_plaindate(ctx,"until"); TsPlainDate* b=coerce_plaindate_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return plaindate_diff(a->iso_year,a->iso_month,a->iso_day,b->iso_year,b->iso_month,b->iso_day,(argc>=2&&argv)?argv[1]:nullptr);
}
TsValue* ts_temporal_plaindate_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* a=require_plaindate(ctx,"since"); TsPlainDate* b=coerce_plaindate_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return plaindate_diff(b->iso_year,b->iso_month,b->iso_day,a->iso_year,a->iso_month,a->iso_day,(argc>=2&&argv)?argv[1]:nullptr);
}
}

// ===================== Arithmetic: PlainDateTime =====================
static long long pdt_time_ns(TsPlainDateTime* d){
    return ((((long long)d->iso_hour*60+d->iso_minute)*60+d->iso_second)*1000000000LL)
        + (long long)d->iso_ms*1000000LL + (long long)d->iso_us*1000LL + d->iso_ns;
}
static long long dur_time_ns(TsDuration* d){
    return d->hours*3600000000000LL + d->minutes*60000000000LL + d->seconds*1000000000LL
        + d->milliseconds*1000000LL + d->microseconds*1000LL + d->nanoseconds;
}
extern "C" {
// Temporal.Now.instant() — the current instant.
TsValue* ts_temporal_now_instant_native(void* ctx,int argc,TsValue** argv){
    (void)ctx;(void)argc;(void)argv;
    using namespace std::chrono;
    long long totalNs = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    return ts_value_make_object(TsInstant::Create(totalNs/1000000LL, (int)(totalNs%1000000LL)));
}
// Temporal.Now.zonedDateTimeISO(timeZone?) — current zoned datetime (fixed offset/UTC).
TsValue* ts_temporal_now_zoneddatetimeiso_native(void* ctx,int argc,TsValue** argv){
    (void)ctx;
    using namespace std::chrono;
    long long totalNs = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    long long ms = totalNs/1000000LL; int sub=(int)(totalNs%1000000LL);
    int off=0; bool utc=true;
    TsValue* tzv=(argc>=1&&argv)?argv[0]:nullptr;
    if(tzv && !ts_value_is_undefined(tzv)){
        std::string tz;
        if(tsvalue_to_stdstring(tzv,&tz) && !parse_timezone(tz.c_str(),&off,&utc)){
            ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Now.zonedDateTimeISO: unsupported time zone")); return ts_value_make_undefined();
        }
    }
    return ts_value_make_object(TsZonedDateTime::Create(ms, sub, off, utc));
}
// Temporal.Instant.fromEpochNanoseconds(epochNanoseconds: bigint).
TsValue* ts_temporal_instant_fromEpochNs_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; TsValue* a0=(argc>=1&&argv)?argv[0]:nullptr; void* raw=a0?ts_nanbox_safe_unbox(a0):nullptr;
    if(!raw || (*(uint32_t*)raw!=0x42494749 && *(uint32_t*)((char*)raw+16)!=0x42494749)){
        ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.fromEpochNanoseconds: argument must be a BigInt")); return ts_value_make_undefined();
    }
    void* str=ts_bigint_to_string(raw,10); const char* u=str?((TsString*)str)->ToUtf8():nullptr;
    long long ms; int sub;
    if(!u || !ns_string_to_ms_sub(u,&ms,&sub) || !instant_epoch_in_limits(ms,sub)){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.fromEpochNanoseconds: out of range")); return ts_value_make_undefined();
    }
    return ts_value_make_object(TsInstant::Create(ms,sub));
}
// NOTE: Temporal.Duration.compare is deferred — a correct implementation needs
// relativeTo handling (calendar/time-zone durations), and a naive time-only
// compare regresses the relativeTo conformance tests.
}
static TsPlainDateTime* coerce_plaindatetime_arg(TsValue* v){
    TsPlainDateTime* p = as_plaindatetime(v?ts_nanbox_safe_unbox(v):nullptr);
    if(p) return p;
    TsValue* c = ts_temporal_plaindatetime_from(v?1:0,&v);
    return as_plaindatetime(ts_nanbox_safe_unbox(c));
}
static TsValue* pdt_add(TsPlainDateTime* dt, TsDuration* d, int sign){
    const long long DAY=86400000000000LL;
    long long t = pdt_time_ns(dt) + sign*dur_time_ns(d);
    long long carry = t/DAY; long long rem=t%DAY; if(rem<0){rem+=DAY;carry--;}
    int Y,M,D; add_iso_date(dt->iso_year,dt->iso_month,dt->iso_day, sign*d->years, sign*d->months, sign*d->weeks, sign*d->days+carry, &Y,&M,&D);
    if(!iso_date_valid(Y,M,D)||!iso_date_in_limits(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime arithmetic: result out of range")); return ts_value_make_undefined(); }
    int h=(int)(rem/3600000000000LL); rem%=3600000000000LL; int mi=(int)(rem/60000000000LL); rem%=60000000000LL;
    int s=(int)(rem/1000000000LL); rem%=1000000000LL; int ms=(int)(rem/1000000LL); rem%=1000000LL; int us=(int)(rem/1000LL); int ns=(int)(rem%1000LL);
    { long long tns=(long long)h*3600000000000LL+(long long)mi*60000000000LL+(long long)s*1000000000LL+(long long)ms*1000000LL+(long long)us*1000LL+ns;
      if(!iso_datetime_in_limits(Y,M,D,tns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime: result is outside the representable range")); return ts_value_make_undefined(); } }
    return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,h,mi,s,ms,us,ns));
}
static TsValue* pdt_diff(TsPlainDateTime* a, TsPlainDateTime* b, const std::string& lu){
    const long long DAY=86400000000000LL;
    long long dateDays = iso_days_from_civil(b->iso_year,b->iso_month,b->iso_day) - iso_days_from_civil(a->iso_year,a->iso_month,a->iso_day);
    long long timeNs = pdt_time_ns(b) - pdt_time_ns(a);
    long long days = dateDays, t = timeNs;
    if(days>0 && t<0){ days--; t+=DAY; } else if(days<0 && t>0){ days++; t-=DAY; }
    int tsign = t<0?-1:1; long long at=t<0?-t:t;
    long long h=at/3600000000000LL; at%=3600000000000LL; long long mi=at/60000000000LL; at%=60000000000LL;
    long long s=at/1000000000LL; at%=1000000000LL; long long ms=at/1000000LL; at%=1000000LL; long long us=at/1000LL; long long ns=at%1000LL;
    if(lu=="day"||lu=="days"){
        return ts_value_make_object(TsDuration::Create(0,0,0, days, tsign*h, tsign*mi, tsign*s, tsign*ms, tsign*us, tsign*ns));
    }
    if(lu=="hour"||lu=="hours"||lu=="minute"||lu=="minutes"||lu=="second"||lu=="seconds"||lu=="millisecond"||lu=="milliseconds"||lu=="microsecond"||lu=="microseconds"||lu=="nanosecond"||lu=="nanoseconds"){
        long long totalNs=days*DAY + t; long long sg=totalNs<0?-1:1, av=totalNs<0?-totalNs:totalNs;
        bool ok2; long long Lns=unit_ns(lu,&ok2); if(!ok2)Lns=3600000000000LL;
        long long uns[6]={3600000000000LL,60000000000LL,1000000000LL,1000000LL,1000LL,1LL}; long long o[6]={0,0,0,0,0,0};
        for(int i=0;i<6;i++){ if(uns[i]<=Lns){ o[i]=(av/uns[i])*sg; av%=uns[i]; } }
        return ts_value_make_object(TsDuration::Create(0,0,0,0, o[0],o[1],o[2],o[3],o[4],o[5]));
    }
    // year/month/week: convert the day count to calendar units (adjusted end date).
    long long acivil = iso_days_from_civil(a->iso_year,a->iso_month,a->iso_day);
    int ey,em,ed; iso_civil_from_days(acivil+days, &ey,&em,&ed);
    long long yr,mo,wk,dy; diff_iso_date(a->iso_year,a->iso_month,a->iso_day, ey,em,ed, lu, &yr,&mo,&wk,&dy);
    return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy, tsign*h, tsign*mi, tsign*s, tsign*ms, tsign*us, tsign*ns));
}
// Split a signed time-of-day nanosecond count into a Duration's time fields.
static void split_time_ns(long long t, long long* h,long long* mi,long long* s,long long* ms,long long* us,long long* ns){
    long long sg=t<0?-1:1, a=t<0?-t:t;
    *h=sg*(a/3600000000000LL); a%=3600000000000LL; *mi=sg*(a/60000000000LL); a%=60000000000LL;
    *s=sg*(a/1000000000LL); a%=1000000000LL; *ms=sg*(a/1000000LL); a%=1000000LL; *us=sg*(a/1000LL); *ns=sg*(a%1000LL);
}
// Rounding/diff core with options already parsed and largest already resolved
// (no opts re-read — callers that have already read the options pass them here).
static TsValue* pdt_diff_rounded(TsPlainDateTime* a, TsPlainDateTime* b, const std::string& smallest, const std::string& largest, const std::string& mode, long long inc){
    const long long DAY=86400000000000LL;
    // No-rounding default -> unchanged fast path.
    if((smallest=="nanosecond"||smallest=="nanoseconds") && mode=="trunc" && inc<=1) return pdt_diff(a,b,largest);
    long long dateDays = iso_days_from_civil(b->iso_year,b->iso_month,b->iso_day) - iso_days_from_civil(a->iso_year,a->iso_month,a->iso_day);
    long long timeNs = pdt_time_ns(b) - pdt_time_ns(a);
    // smallestUnit week or smaller rounds the combined ns exactly (PDT day == 24h);
    // only month/year need the calendar nudge.
    bool sIsCalendar = (date_unit_rank(smallest)>=4);
    if(!sIsCalendar){
        long long totalNs = dateDays*DAY + timeNs;   // PlainDateTime day == 24h
        long long sNs;
        if(smallest=="week"||smallest=="weeks") sNs=7*DAY;
        else if(smallest=="day"||smallest=="days") sNs=DAY;
        else { bool ok; sNs=unit_ns(smallest,&ok); if(!ok) sNs=1; }
        long long r=round_signed(totalNs, sNs*(inc>0?inc:1), mode);
        long long days=r/DAY, t=r%DAY;
        long long h,mi,s,ms,us,ns; split_time_ns(t,&h,&mi,&s,&ms,&us,&ns);
        if(date_unit_rank(largest)>=4){ // month/year: balance days to calendar
            int ey,em,ed; iso_civil_from_days(iso_days_from_civil(a->iso_year,a->iso_month,a->iso_day)+days,&ey,&em,&ed);
            long long yr,mo,wk,dy; diff_iso_date(a->iso_year,a->iso_month,a->iso_day,ey,em,ed,largest,&yr,&mo,&wk,&dy);
            return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy,h,mi,s,ms,us,ns));
        }
        if(largest=="week"||largest=="weeks"){ long long wk=days/7, dy=days%7; return ts_value_make_object(TsDuration::Create(0,0,wk,dy,h,mi,s,ms,us,ns)); }
        if(largest=="day"||largest=="days") return ts_value_make_object(TsDuration::Create(0,0,0,days,h,mi,s,ms,us,ns));
        // largest < day: fold days into the time fields
        split_time_ns(r, &h,&mi,&s,&ms,&us,&ns);
        return ts_value_make_object(TsDuration::Create(0,0,0,0,h,mi,s,ms,us,ns));
    }
    // DATE smallestUnit: bring the end date toward the start by the time sign, then nudge
    // (sub-day fraction is dropped — exact for trunc, the default for date rounding).
    long long days=dateDays, t=timeNs;
    if(days>0 && t<0) days--; else if(days<0 && t>0) days++;
    int ey,em,ed; iso_civil_from_days(iso_days_from_civil(a->iso_year,a->iso_month,a->iso_day)+days,&ey,&em,&ed);
    long long yr,mo,wk,dy; bool _re=false; round_date_duration(a->iso_year,a->iso_month,a->iso_day,ey,em,ed,smallest,largest,inc,mode,&yr,&mo,&wk,&dy,&_re);
    if(_re){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: rounded date is outside the valid ISO range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy,0,0,0,0,0,0));
}
// Reads & validates the diff options once, resolves largestUnit, then rounds.
static TsValue* pdt_diff_opts(TsPlainDateTime* a, TsPlainDateTime* b, TsValue* opts, const char* defLargest="day"){
    std::string smallest,largest,mode; long long inc;
    read_validated_diff_opts(opts,1,10,"nanosecond","auto",&smallest,&largest,&mode,&inc);
    validate_diff_time_increment(smallest, inc);
    if(largest=="auto") largest = (date_unit_rank(smallest)>date_unit_rank(defLargest)) ? smallest : std::string(defLargest);
    return pdt_diff_rounded(a,b,smallest,largest,mode,inc);
}
extern "C" {
TsValue* ts_temporal_plaindatetime_add_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* dt=require_plaindatetime(ctx,"add"); bool _ovrej=validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    if(_ovrej){ long long ty=dt->iso_year+d->years, tm=dt->iso_month+d->months; while(tm>12){tm-=12;ty++;} while(tm<1){tm+=12;ty--;}
        if(dt->iso_day > iso_days_in_month((int)ty,(int)tm)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.prototype.add: date does not exist with overflow reject")); return ts_value_make_undefined(); } }
    return pdt_add(dt,d,1);
}
TsValue* ts_temporal_plaindatetime_subtract_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* dt=require_plaindatetime(ctx,"subtract"); bool _ovrej=validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    if(_ovrej){ long long ty=dt->iso_year-d->years, tm=dt->iso_month-d->months; while(tm>12){tm-=12;ty++;} while(tm<1){tm+=12;ty--;}
        if(dt->iso_day > iso_days_in_month((int)ty,(int)tm)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.prototype.subtract: date does not exist with overflow reject")); return ts_value_make_undefined(); } }
    return pdt_add(dt,d,-1);
}
TsValue* ts_temporal_plaindatetime_until_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* a=require_plaindatetime(ctx,"until"); TsPlainDateTime* b=coerce_plaindatetime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return pdt_diff_opts(a,b,(argc>=2&&argv)?argv[1]:nullptr);
}
TsValue* ts_temporal_plaindatetime_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* a=require_plaindatetime(ctx,"since"); TsPlainDateTime* b=coerce_plaindatetime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return pdt_diff_opts(b,a,(argc>=2&&argv)?argv[1]:nullptr);
}
}

// ======================= Arithmetic: Instant =======================
// Build a time-only Duration from a sign-aligned (ms, sub-ns) magnitude.
static TsValue* duration_from_ms_sub(long long ms, long long subNs, const std::string& largest){
    int sign = (ms<0||subNs<0)?-1:1; long long ams=ms<0?-ms:ms; long long asub=subNs<0?-subNs:subNs;
    // microsecond / nanosecond largestUnit: fold the whole millisecond part down too.
    if(largest=="microsecond"||largest=="microseconds"){ long long us=ams*1000LL+asub/1000LL, ns=asub%1000LL; return ts_value_make_object(TsDuration::Create(0,0,0,0,0,0,0,0, sign*us, sign*ns)); }
    if(largest=="nanosecond"||largest=="nanoseconds"){ long long ns=ams*1000000LL+asub; return ts_value_make_object(TsDuration::Create(0,0,0,0,0,0,0,0,0, sign*ns)); }
    long long h=0,mi=0,s=0,msr=0; long long rem=ams;
    if(largest=="hour"||largest=="hours"){ h=rem/3600000; rem%=3600000; mi=rem/60000; rem%=60000; s=rem/1000; msr=rem%1000; }
    else if(largest=="minute"||largest=="minutes"){ mi=rem/60000; rem%=60000; s=rem/1000; msr=rem%1000; }
    else if(largest=="millisecond"||largest=="milliseconds"){ msr=rem; }
    else { s=rem/1000; msr=rem%1000; } // second (default)
    long long us=asub/1000, ns=asub%1000;
    return ts_value_make_object(TsDuration::Create(0,0,0,0, sign*h, sign*mi, sign*s, sign*msr, sign*us, sign*ns));
}
static void instant_add_time(long long ems,int esub, TsDuration* d, int sign, long long* oms, int* osub){
    long long addNs = dur_time_ns(d);
    long long newMs = ems + sign*(addNs/1000000LL);
    long long newSub = (long long)esub + sign*(addNs%1000000LL);
    newMs += newSub/1000000LL; newSub %= 1000000LL;
    if(newMs>0 && newSub<0){ newMs--; newSub+=1000000LL; } else if(newMs<0 && newSub>0){ newMs++; newSub-=1000000LL; }
    *oms=newMs; *osub=(int)newSub;
}
static TsInstant* coerce_instant_arg(TsValue* v){
    TsInstant* p = as_instant(v?ts_nanbox_safe_unbox(v):nullptr);
    if(p) return p;
    TsValue* c = ts_temporal_instant_from(v?1:0,&v);
    return as_instant(ts_nanbox_safe_unbox(c));
}
extern "C" {
TsValue* ts_temporal_instant_add_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"add"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    if(d->years||d->months||d->weeks||d->days){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.add: duration must be time-only")); return ts_value_make_undefined(); }
    long long oms; int osub; instant_add_time(it->epoch_ms,it->sub_ns,d,1,&oms,&osub);
    if(!instant_epoch_in_limits(oms,osub)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.add: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create(oms,osub));
}
TsValue* ts_temporal_instant_subtract_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"subtract"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    if(d->years||d->months||d->weeks||d->days){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.subtract: duration must be time-only")); return ts_value_make_undefined(); }
    long long oms; int osub; instant_add_time(it->epoch_ms,it->sub_ns,d,-1,&oms,&osub);
    if(!instant_epoch_in_limits(oms,osub)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.subtract: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create(oms,osub));
}
// Instant diff with smallestUnit rounding (time units only; default largestUnit second).
static TsValue* instant_diff_rounded(long long ms, long long sub, TsValue* opts){
    std::string largest,smallest,mode; long long inc;
    read_validated_diff_opts(opts,1,6,"nanosecond","auto",&smallest,&largest,&mode,&inc);
    validate_diff_time_increment(smallest, inc);
    // largestUnit "auto" resolves to the coarser of smallestUnit and "second".
    if(largest=="auto"){
        bool oa,ob; long long sN=unit_ns(smallest,&oa), secN=unit_ns("second",&ob);
        largest = (oa && sN>secN) ? smallest : std::string("second");
    }
    long long totalNs = ms*1000000LL + sub;
    bool ok; long long sNs=unit_ns(smallest,&ok);
    if(ok && (sNs>1 || inc>1)){
        totalNs = round_signed(totalNs, sNs*(inc>0?inc:1), mode);
    }
    return duration_from_ms_sub(totalNs/1000000LL, totalNs%1000000LL, largest);
}
TsValue* ts_temporal_instant_until_native(void* ctx,int argc,TsValue** argv){
    TsInstant* a=require_instant(ctx,"until"); TsInstant* b=coerce_instant_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    long long ms=b->epoch_ms-a->epoch_ms; long long sub=(long long)b->sub_ns-a->sub_ns;
    if(ms>0&&sub<0){ms--;sub+=1000000;} else if(ms<0&&sub>0){ms++;sub-=1000000;}
    return instant_diff_rounded(ms,sub,(argc>=2&&argv)?argv[1]:nullptr);
}
TsValue* ts_temporal_instant_since_native(void* ctx,int argc,TsValue** argv){
    TsInstant* a=require_instant(ctx,"since"); TsInstant* b=coerce_instant_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    long long ms=a->epoch_ms-b->epoch_ms; long long sub=(long long)a->sub_ns-b->sub_ns;
    if(ms>0&&sub<0){ms--;sub+=1000000;} else if(ms<0&&sub>0){ms++;sub-=1000000;}
    return instant_diff_rounded(ms,sub,(argc>=2&&argv)?argv[1]:nullptr);
}
// Temporal.Instant.prototype.round(roundTo) — round the epoch to a time unit
// (hour..nanosecond; no calendar units). The rounding quantum divides a day, so
// the (epoch_ms, sub_ns) arithmetic stays within int64.
TsValue* ts_temporal_instant_round_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"round");
    TsValue* roundTo=(argc>=1&&argv)?argv[0]:nullptr;
    if(!roundTo||ts_value_is_undefined(roundTo)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.round: roundTo is required")); return ts_value_make_undefined(); }
    std::string unit; long long inc=1; std::string mode="halfExpand";
    if(!parse_round_options(roundTo,&unit,&inc,&mode,1,6)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.round: smallestUnit is required")); return ts_value_make_undefined(); }
    bool ok; long long unitNs=unit_ns(unit,&ok);
    if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.round: invalid smallestUnit")); return ts_value_make_undefined(); }
    long long q=unitNs*inc; const long long DAY=86400000000000LL;
    if(q>DAY || DAY%q!=0){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.round: invalid roundingIncrement")); return ts_value_make_undefined(); }
    long long ms=it->epoch_ms, sns=it->sub_ns;
    if(sns<0){ ms-=1; sns+=1000000; }   // FLOOR -> sns in [0,999999]
    long long rMs, rSns;
    if(q<1000000){
        long long r=round_signed(sns,q,mode);
        rMs=ms + r/1000000; rSns=r%1000000;
    } else {
        long long qMs=q/1000000;
        long long base=ms - (((ms%qMs)+qMs)%qMs);   // floor toward -inf
        long long remNs=(ms-base)*1000000 + sns;     // ns past base, in [0, qMs*1e6)
        long long roundedRem=round_nonneg(remNs, qMs*1000000, mode);
        rMs=base + roundedRem/1000000; rSns=0;
    }
    if(!instant_ms_in_range(rMs)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.round: instant out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create(rMs,(int)rSns));
}
}

// ==================== Arithmetic: ZonedDateTime ====================
// Fixed-offset only: do the arithmetic on the local wall-clock, then re-derive
// the epoch (no DST transitions to worry about).
static TsValue* zdt_add(TsZonedDateTime* z, TsDuration* d, int sign){
    const long long DAY=86400000000000LL;
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    long long timeNs = ((((long long)h*60+mi)*60+s)*1000000000LL) + (long long)ms*1000000LL + (long long)us*1000LL + ns;
    timeNs += sign*dur_time_ns(d);
    long long carry = timeNs/DAY; long long rem=timeNs%DAY; if(rem<0){rem+=DAY;carry--;}
    int nY,nM,nD; add_iso_date(Y,M,D, sign*d->years, sign*d->months, sign*d->weeks, sign*d->days+carry, &nY,&nM,&nD);
    if(!iso_date_valid(nY,nM,nD)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime arithmetic: result out of range")); return ts_value_make_undefined(); }
    int nh=(int)(rem/3600000000000LL); rem%=3600000000000LL; int nmi=(int)(rem/60000000000LL); rem%=60000000000LL;
    int nss=(int)(rem/1000000000LL); rem%=1000000000LL; int nms=(int)(rem/1000000LL); rem%=1000000LL; int nus=(int)(rem/1000LL); int nns=(int)(rem%1000LL);
    long long localMs = iso_days_from_civil(nY,nM,nD)*86400000LL + (long long)nh*3600000 + (long long)nmi*60000 + (long long)nss*1000 + nms;
    long long epoch_ms = localMs - (long long)z->offset_minutes*60000LL;
    return ts_value_make_object(TsZonedDateTime::Create(epoch_ms, nus*1000+nns, z->offset_minutes, z->is_utc));
}
static TsValue* zdt_diff(TsZonedDateTime* a, TsZonedDateTime* b, const std::string& lu){
    const long long DAY=86400000000000LL;
    int aY,aM,aD,ah,ami,as,ams,aus,ans; zdt_local(a,&aY,&aM,&aD,&ah,&ami,&as,&ams,&aus,&ans);
    int bY,bM,bD,bh,bmi,bs,bms,bus,bns; zdt_local(b,&bY,&bM,&bD,&bh,&bmi,&bs,&bms,&bus,&bns);
    long long dateDays = iso_days_from_civil(bY,bM,bD) - iso_days_from_civil(aY,aM,aD);
    long long timeNs = (((((long long)bh*60+bmi)*60+bs)*1000000000LL)+(long long)bms*1000000LL+(long long)bus*1000LL+bns)
                     - (((((long long)ah*60+ami)*60+as)*1000000000LL)+(long long)ams*1000000LL+(long long)aus*1000LL+ans);
    long long days=dateDays, t=timeNs;
    if(days>0&&t<0){days--;t+=DAY;} else if(days<0&&t>0){days++;t-=DAY;}
    int tsign=t<0?-1:1; long long at=t<0?-t:t;
    long long hh=at/3600000000000LL; at%=3600000000000LL; long long mm=at/60000000000LL; at%=60000000000LL;
    long long ss=at/1000000000LL; at%=1000000000LL; long long mms=at/1000000LL; at%=1000000LL; long long uus=at/1000LL; long long nns=at%1000LL;
    if(lu=="year"||lu=="years"||lu=="month"||lu=="months"||lu=="week"||lu=="weeks"){
        long long acivil=iso_days_from_civil(aY,aM,aD); int ey,em,ed; iso_civil_from_days(acivil+days,&ey,&em,&ed);
        long long yr,mo,wk,dy; diff_iso_date(aY,aM,aD,ey,em,ed,lu,&yr,&mo,&wk,&dy);
        return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy, tsign*hh,tsign*mm,tsign*ss,tsign*mms,tsign*uus,tsign*nns));
    }
    if(lu=="day"||lu=="days")
        return ts_value_make_object(TsDuration::Create(0,0,0,days, tsign*hh,tsign*mm,tsign*ss,tsign*mms,tsign*uus,tsign*nns));
    // time largestUnit (hour..ns): fold the days into the largest time unit.
    long long totalNs=days*DAY + t; long long sg=totalNs<0?-1:1, av=totalNs<0?-totalNs:totalNs;
    bool ok2; long long Lns=unit_ns(lu,&ok2); if(!ok2)Lns=3600000000000LL;
    long long uns[6]={3600000000000LL,60000000000LL,1000000000LL,1000000LL,1000LL,1LL}; long long o[6]={0,0,0,0,0,0};
    for(int i=0;i<6;i++){ if(uns[i]<=Lns){ o[i]=(av/uns[i])*sg; av%=uns[i]; } }
    return ts_value_make_object(TsDuration::Create(0,0,0,0, o[0],o[1],o[2],o[3],o[4],o[5]));
}
extern "C" {
TsValue* ts_temporal_zdt_add_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"add"); validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    return zdt_add(z,d,1);
}
TsValue* ts_temporal_zdt_subtract_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"subtract"); validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    return zdt_add(z,d,-1);
}
static TsZonedDateTime* coerce_zdt_arg(TsValue* v){
    TsZonedDateTime* z=as_zoneddatetime(v?ts_nanbox_safe_unbox(v):nullptr); if(z) return z;
    TsValue* args[1]={v}; TsValue* c=ts_temporal_zdt_from(v?1:0,args); return as_zoneddatetime(ts_nanbox_safe_unbox(c));
}
// ZDT diff via the local datetimes (valid for fixed-offset/UTC zones), with
// smallestUnit rounding (default largestUnit hour). No-rounding -> existing zdt_diff.
static TsValue* zdt_diff_opts(TsZonedDateTime* a, TsZonedDateTime* b, TsValue* opts){
    std::string smallest,largest,mode; long long inc;
    read_validated_diff_opts(opts,1,10,"nanosecond","hour",&smallest,&largest,&mode,&inc);
    validate_diff_time_increment(smallest, inc);   // preserved from the former pdt_diff_opts re-read
    // largestUnit "auto" resolves to the larger of smallestUnit and "hour" (was done by the re-read).
    if(largest=="auto") largest = (date_unit_rank(smallest)>date_unit_rank("hour")) ? smallest : std::string("hour");
    if((smallest=="nanosecond"||smallest=="nanoseconds")&&mode=="trunc"&&inc<=1) return zdt_diff(a,b,largest);
    int aY,aM,aD,ah,ami,as_,ams,aus,ans; zdt_local(a,&aY,&aM,&aD,&ah,&ami,&as_,&ams,&aus,&ans);
    int bY,bM,bD,bh,bmi,bs,bms,bus,bns; zdt_local(b,&bY,&bM,&bD,&bh,&bmi,&bs,&bms,&bus,&bns);
    TsPlainDateTime* pa=TsPlainDateTime::Create(aY,aM,aD,ah,ami,as_,ams,aus,ans);
    TsPlainDateTime* pb=TsPlainDateTime::Create(bY,bM,bD,bh,bmi,bs,bms,bus,bns);
    return pdt_diff_rounded(pa,pb,smallest,largest,mode,inc);   // options already read — no second pass
}
TsValue* ts_temporal_zdt_until_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* a=require_zoneddatetime(ctx,"until"); TsZonedDateTime* b=coerce_zdt_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return zdt_diff_opts(a,b,(argc>=2&&argv)?argv[1]:nullptr);
}
TsValue* ts_temporal_zdt_since_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* a=require_zoneddatetime(ctx,"since"); TsZonedDateTime* b=coerce_zdt_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return zdt_diff_opts(b,a,(argc>=2&&argv)?argv[1]:nullptr);
}
static TsValue* zdt_from_local(int Y,int M,int D,int h,int mi,int s,int ms,int us,int ns,int off,bool utc){
    long long localMs=iso_days_from_civil(Y,M,D)*86400000LL+(long long)h*3600000+(long long)mi*60000+(long long)s*1000+ms;
    long long epoch_ms=localMs-(long long)off*60000LL;
    if(!instant_epoch_in_limits(epoch_ms, us*1000+ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime: result is outside the representable range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(epoch_ms, us*1000+ns, off, utc));
}
TsValue* ts_temporal_zdt_withTimeZone_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"withTimeZone");
    TsValue* tzf=(argc>=1&&argv)?argv[0]:nullptr; void* tzr=tzf?ts_nanbox_safe_unbox(tzf):nullptr;
    if(!tzr||(*(uint32_t*)tzr!=0x53545247&&*(uint32_t*)tzr!=0x434F4E53)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.withTimeZone: timeZone must be a string")); return ts_value_make_undefined(); }
    const char* tu=((TsString*)ts_value_get_string(tzf))->ToUtf8(); int off; bool utc;
    if(!tu||!parse_timezone(tu,&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.prototype.withTimeZone: unsupported time zone")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(z->epoch_ms, z->sub_ns, off, utc));  // same instant
}
TsValue* ts_temporal_zdt_withCalendar_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"withCalendar");
    TsValue* cf=(argc>=1&&argv)?argv[0]:nullptr; validate_calendar_slot_arg(cf);
    return ts_value_make_object(TsZonedDateTime::Create(z->epoch_ms,z->sub_ns,z->offset_minutes,z->is_utc));
}
// getTimeZoneTransition(directionParam): validate the required direction, then
// return null — ts-aot time zones are offset/UTC based and have no transitions.
TsValue* ts_temporal_zdt_getTimeZoneTransition_native(void* ctx,int argc,TsValue** argv){
    require_zoneddatetime(ctx,"getTimeZoneTransition");
    TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr;
    if(!arg || ts_value_is_undefined(arg)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.getTimeZoneTransition: direction is required")); return ts_value_make_undefined(); }
    std::string dir;
    void* raw=ts_nanbox_safe_unbox(arg);
    bool isStr = raw && (uintptr_t)raw>=4096 && (uintptr_t)raw<=0x00007FFFFFFFFFFFULL && (*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53);
    if(isStr){ dir=((TsString*)ts_value_get_string(arg))->ToUtf8(); }
    else if(raw){ TsValue* d=ts_object_get_property(raw,"direction");
        if(!d||ts_value_is_undefined(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","getTimeZoneTransition: direction is required")); return ts_value_make_undefined(); }
        if(!option_to_string(d,&dir)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","getTimeZoneTransition: invalid direction")); return ts_value_make_undefined(); } }
    else { ts_throw((TsValue*)ts_error_create_typed("TypeError","getTimeZoneTransition: direction must be a string or object")); return ts_value_make_undefined(); }
    if(dir!="next" && dir!="previous"){ ts_throw((TsValue*)ts_error_create_typed("RangeError","getTimeZoneTransition: direction must be \"next\" or \"previous\"")); return ts_value_make_undefined(); }
    return ts_value_make_null();
}
// Validate that a calendar argument is iso8601 (the only supported calendar),
// throwing RangeError otherwise. A bare non-string is left to the caller.
static void require_iso_calendar(TsValue* cf, const char* method){
    (void)method;
    validate_calendar_slot_arg(cf);   // undefined->ok; non-string->TypeError; string->iso8601 or ISO-string
}
TsValue* ts_temporal_plaindate_withCalendar_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* d=require_plaindate(ctx,"withCalendar");
    require_iso_calendar((argc>=1&&argv)?argv[0]:nullptr,"Temporal.PlainDate.prototype.withCalendar: only iso8601 is supported");
    return ts_value_make_object(TsPlainDate::Create(d->iso_year,d->iso_month,d->iso_day));
}
TsValue* ts_temporal_plaindatetime_withCalendar_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"withCalendar");
    require_iso_calendar((argc>=1&&argv)?argv[0]:nullptr,"Temporal.PlainDateTime.prototype.withCalendar: only iso8601 is supported");
    return ts_value_make_object(TsPlainDateTime::Create(d->iso_year,d->iso_month,d->iso_day,d->iso_hour,d->iso_minute,d->iso_second,d->iso_ms,d->iso_us,d->iso_ns));
}
TsValue* ts_temporal_zdt_withPlainTime_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"withPlainTime");
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    int nh=0,nmi=0,nss=0,nms=0,nus=0,nns=0;
    if(argc>=1&&argv&&argv[0]&&!ts_value_is_undefined(argv[0])){
        TsPlainTime* pt=coerce_plaintime_arg(argv[0]);
        if(pt){ nh=pt->iso_hour;nmi=pt->iso_minute;nss=pt->iso_second;nms=pt->iso_millisecond;nus=pt->iso_microsecond;nns=pt->iso_nanosecond; }
    }
    return zdt_from_local(Y,M,D,nh,nmi,nss,nms,nus,nns,z->offset_minutes,z->is_utc);
}
TsValue* ts_temporal_zdt_with_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"with");
    validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr);
    { static const char* OFF[4]={"prefer","use","ignore","reject"};
      static const char* DIS[4]={"compatible","earlier","later","reject"};
      TsValue* o2=(argc>=2&&argv)?argv[1]:nullptr;
      read_enum_option(o2,"offset","prefer",OFF,4);
      read_enum_option(o2,"disambiguation","compatible",DIS,4); }
    void* raw=(argc>=1&&argv&&argv[0])?ts_nanbox_safe_unbox(argv[0]):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||is_temporal_typed_object(raw)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    auto rd=[&](const char* k,int cur)->int{ TsValue* f=ts_object_get_property(raw,k); if(!f||ts_value_is_undefined(f))return cur; double d=ts_to_number(f); if(std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: property must be a finite number")); } if(d!=d)return cur; return (int)std::trunc(d); };
    Y=rd("year",Y); int bagM=read_bag_month(raw); if(bagM>=1)M=bagM; D=rd("day",D);
    h=rd("hour",h); mi=rd("minute",mi); s=rd("second",s); ms=rd("millisecond",ms); us=rd("microsecond",us); ns=rd("nanosecond",ns);
    { TsValue* offf=ts_object_get_property(raw,"offset"); if(offf&&!ts_value_is_undefined(offf)){ std::string os; if(!tsvalue_to_stdstring(offf,&os)||!valid_offset_field(os.c_str())){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.prototype.with: invalid offset string")); return ts_value_make_undefined(); } } }
    if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(Y,M); if(D<1)D=1; if(D>dim)D=dim;
    const int lim[6]={23,59,59,999,999,999}; int* tp[6]={&h,&mi,&s,&ms,&us,&ns}; for(int i=0;i<6;i++){ if(*tp[i]<0)*tp[i]=0; if(*tp[i]>lim[i])*tp[i]=lim[i]; }
    return zdt_from_local(Y,M,D,h,mi,s,ms,us,ns,z->offset_minutes,z->is_utc);
}
}

// ====================== Arithmetic: Duration ======================
static TsValue* duration_add_impl(TsDuration* a, TsDuration* b, int bsign){
    if(a->years||a->months||a->weeks||b->years||b->months||b->weeks){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration arithmetic with calendar units requires relativeTo"));
        return ts_value_make_undefined();
    }
    const long long DAY=86400000000000LL;
    // Default largestUnit = the largest non-zero unit of either operand; the result
    // must not balance UP past it (e.g. {hours:24} stays 24h, not 1 day).
    auto rankOf=[](TsDuration* d)->int{ if(d->days)return 4; if(d->hours)return 5; if(d->minutes)return 6; if(d->seconds)return 7; if(d->milliseconds)return 8; if(d->microseconds)return 9; if(d->nanoseconds)return 10; return 11; };
    int ra=rankOf(a), rb=rankOf(b), rank=ra<rb?ra:rb;
    long long totalDays = a->days + bsign*b->days;
    long long ns = dur_time_ns(a) + bsign*dur_time_ns(b);
    if(rank<=4){   // a day (or larger) is present -> whole days absorb the time
        totalDays += ns/DAY; ns %= DAY;
        if(totalDays>0 && ns<0){ totalDays--; ns+=DAY; } else if(totalDays<0 && ns>0){ totalDays++; ns-=DAY; }
    }
    int sign=(totalDays<0||(totalDays==0&&ns<0))?-1:1; long long ad=totalDays<0?-totalDays:totalDays; long long an=ns<0?-ns:ns;
    long long h=0,mi=0,s=0,ms=0,us=0,nn=0;
    if(rank<=5){ h=an/3600000000000LL; an%=3600000000000LL; }
    if(rank<=6){ mi=an/60000000000LL; an%=60000000000LL; }
    if(rank<=7){ s=an/1000000000LL; an%=1000000000LL; }
    if(rank<=8){ ms=an/1000000LL; an%=1000000LL; }
    if(rank<=9){ us=an/1000LL; an%=1000LL; }
    nn=an;
    long long fr[10]={0,0,0, sign*ad, sign*h, sign*mi, sign*s, sign*ms, sign*us, sign*nn};
    if(!duration_in_range(fr)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration: result is out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsDuration::Create(0,0,0, sign*ad, sign*h, sign*mi, sign*s, sign*ms, sign*us, sign*nn));
}
extern "C" {
TsValue* ts_temporal_duration_add_native(void* ctx,int argc,TsValue** argv){
    TsDuration* a=require_duration(ctx,"add"); TsDuration* b=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    return duration_add_impl(a,b,1);
}
TsValue* ts_temporal_duration_subtract_native(void* ctx,int argc,TsValue** argv){
    TsDuration* a=require_duration(ctx,"subtract"); TsDuration* b=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    return duration_add_impl(a,b,-1);
}
// Duration.total({unit}) for time/day units -> a Number (no relativeTo support).
TsValue* ts_temporal_duration_total_native(void* ctx,int argc,TsValue** argv){
    TsDuration* d=require_duration(ctx,"total");
    std::string unit; TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr;
    if(!tsvalue_to_stdstring(arg,&unit)) unit = read_string_option(arg,"unit","");
    double extraNs = 0.0;   // time-part nanoseconds contributed by a calendar-anchored span
    if(d->years||d->months||d->weeks){
        // Calendar units require relativeTo. Anchor to a PlainDate and expand the
        // calendar part to a concrete day count; year/month TOTALS need the
        // fractional-calendar algorithm and are still deferred.
        void* raw = arg ? ts_nanbox_safe_unbox(arg) : nullptr;
        TsValue* relTo = raw ? ts_object_get_property(raw,"relativeTo") : nullptr;
        validate_relativeto_arg(relTo);
        TsPlainDate* rd = (relTo && !ts_value_is_undefined(relTo)) ? coerce_plaindate_arg(relTo) : nullptr;
        if(!rd){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total with calendar units requires relativeTo")); return ts_value_make_undefined(); }
        // ZonedDateTime-style relativeTo (offset / time zone / Z) needs the tz-aware
        // anchoring that isn't implemented yet — defer (RangeError). Pure PlainDate
        // relativeTo (string/bag without offset/tz) is handled below.
        { void* rr = relTo ? ts_nanbox_safe_unbox(relTo) : nullptr; bool zdtLike=false;
          if(rr){ uint32_t rm=*(uint32_t*)rr;
            if(rm==0x53545247||rm==0x434F4E53){ void* sp=ts_value_get_string(relTo); const char* ru=sp?((TsString*)sp)->ToUtf8():nullptr; if(ru&&(strchr(ru,'Z')||strchr(ru,'z')||strchr(ru,'['))) zdtLike=true; }
            else { uint32_t rm16=*(uint32_t*)((char*)rr+16); if(rm16==TsZonedDateTime::MAGIC) zdtLike=true; else { TsValue* tz=ts_object_get_property(rr,"timeZone"); TsValue* of=ts_object_get_property(rr,"offset"); if((tz&&!ts_value_is_undefined(tz))||(of&&!ts_value_is_undefined(of))) zdtLike=true; } } }
          if(zdtLike){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total: ZonedDateTime relativeTo not yet supported")); return ts_value_make_undefined(); } }
        if(unit=="year"||unit=="years"||unit=="month"||unit=="months"){
            // Fractional-calendar total: anchor the whole duration to a PlainDate, count
            // the whole years/months from anchor to the end date, then add the fractional
            // remainder (days past the floor-unit boundary / days spanning one more unit).
            bool isYear=(unit=="year"||unit=="years");
            long long timeNs=(long long)d->hours*3600000000000LL+(long long)d->minutes*60000000000LL+(long long)d->seconds*1000000000LL+(long long)d->milliseconds*1000000LL+(long long)d->microseconds*1000LL+d->nanoseconds;
            long long wholeDaysFromTime=timeNs/86400000000000LL, subDayNs=timeNs%86400000000000LL;
            int gy,gm,gd; add_iso_date(rd->iso_year,rd->iso_month,rd->iso_day, d->years,d->months,d->weeks, d->days+wholeDaysFromTime, &gy,&gm,&gd);
            if(!iso_date_in_limits(gy,gm,gd)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total: result out of range")); return ts_value_make_undefined(); }
            long long startE2=iso_days_from_civil(rd->iso_year,rd->iso_month,rd->iso_day), endE2=iso_days_from_civil(gy,gm,gd);
            long long y2,mo2,wk2,dy2; diff_iso_date(rd->iso_year,rd->iso_month,rd->iso_day, gy,gm,gd, isYear?std::string("year"):std::string("month"), &y2,&mo2,&wk2,&dy2);
            long long whole = isYear ? y2 : mo2;     // diff_iso_date folds years into months for "month"
            int sgn=(endE2>=startE2)?1:-1;
            int mY,mM,mD; add_iso_date(rd->iso_year,rd->iso_month,rd->iso_day, isYear?whole:0, isYear?0:whole, 0,0, &mY,&mM,&mD);
            int n2Y,n2M,n2D; add_iso_date(rd->iso_year,rd->iso_month,rd->iso_day, isYear?(whole+sgn):0, isYear?0:(whole+sgn), 0,0, &n2Y,&n2M,&n2D);
            double midE=(double)iso_days_from_civil(mY,mM,mD), mid2E=(double)iso_days_from_civil(n2Y,n2M,n2D);
            double endPos=(double)endE2 + (double)subDayNs/86400000000000.0;
            double span=mid2E-midE, num=endPos-midE;
            // num and span share the duration's sign, so num/span is the POSITIVE fraction
            // magnitude in [0,1); apply the overall sign so a negative total grows downward.
            double total=(double)whole + ((span!=0.0)? (double)sgn*(num/span) : 0.0);
            return ts_value_make_double(total);
        }
        int ey,em,ed; add_iso_date(rd->iso_year,rd->iso_month,rd->iso_day, d->years,d->months,d->weeks, d->days, &ey,&em,&ed);
        if(!iso_date_in_limits(ey,em,ed)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total: result out of range")); return ts_value_make_undefined(); }
        long long dayDiff = iso_days_from_civil(ey,em,ed) - iso_days_from_civil(rd->iso_year,rd->iso_month,rd->iso_day);
        extraNs = (double)dayDiff*86400000000000.0
            + (double)d->hours*3600000000000.0 + (double)d->minutes*60000000000.0 + (double)d->seconds*1000000000.0
            + (double)d->milliseconds*1000000.0 + (double)d->microseconds*1000.0 + (double)d->nanoseconds;
        if(!(extraNs==extraNs) || extraNs>9.007199254740992e21 || extraNs<-9.007199254740992e21){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total: result out of range")); return ts_value_make_undefined(); }
        double uNs;
        if(unit=="week"||unit=="weeks") uNs=7.0*86400000000000.0;
        else if(unit=="day"||unit=="days") uNs=86400000000000.0;
        else if(unit=="hour"||unit=="hours") uNs=3600000000000.0;
        else if(unit=="minute"||unit=="minutes") uNs=60000000000.0;
        else if(unit=="second"||unit=="seconds") uNs=1000000000.0;
        else if(unit=="millisecond"||unit=="milliseconds") uNs=1000000.0;
        else if(unit=="microsecond"||unit=="microseconds") uNs=1000.0;
        else if(unit=="nanosecond"||unit=="nanoseconds") uNs=1.0;
        else { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total: invalid unit")); return ts_value_make_undefined(); }
        return ts_value_make_double(extraNs/uNs);
    }
    double totalNs = (double)d->days*86400000000000.0 + (double)d->hours*3600000000000.0 + (double)d->minutes*60000000000.0
        + (double)d->seconds*1000000000.0 + (double)d->milliseconds*1000000.0 + (double)d->microseconds*1000.0 + (double)d->nanoseconds;
    double unitNs;
    if(unit=="day"||unit=="days") unitNs=86400000000000.0;
    else if(unit=="hour"||unit=="hours") unitNs=3600000000000.0;
    else if(unit=="minute"||unit=="minutes") unitNs=60000000000.0;
    else if(unit=="second"||unit=="seconds") unitNs=1000000000.0;
    else if(unit=="millisecond"||unit=="milliseconds") unitNs=1000000.0;
    else if(unit=="microsecond"||unit=="microseconds") unitNs=1000.0;
    else if(unit=="nanosecond"||unit=="nanoseconds") unitNs=1.0;
    else { ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total: invalid unit")); return ts_value_make_undefined(); }
    return ts_value_make_double(totalNs/unitNs);
}
}

// ===================== Cross-type conversions =====================
extern "C" {
TsValue* ts_temporal_plaindate_toPlainDateTime_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"toPlainDateTime");
    int h=0,mi=0,s=0,ms=0,us=0,ns=0;
    if(argc>=1&&argv&&argv[0]&&!ts_value_is_undefined(argv[0])){
        TsPlainTime* t=coerce_plaintime_arg(argv[0]);
        if(t){ h=t->iso_hour;mi=t->iso_minute;s=t->iso_second;ms=t->iso_millisecond;us=t->iso_microsecond;ns=t->iso_nanosecond; }
    }
    { long long tns=(long long)h*3600000000000LL+(long long)mi*60000000000LL+(long long)s*1000000000LL+(long long)ms*1000000LL+(long long)us*1000LL+ns;
      if(!iso_datetime_in_limits(pd->iso_year,pd->iso_month,pd->iso_day,tns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.toPlainDateTime: result is outside the representable range")); return ts_value_make_undefined(); } }
    return ts_value_make_object(TsPlainDateTime::Create(pd->iso_year,pd->iso_month,pd->iso_day,h,mi,s,ms,us,ns));
}
TsValue* ts_temporal_plaindate_toPlainYearMonth_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"toPlainYearMonth");
    return ts_value_make_object(TsPlainYearMonth::Create(pd->iso_year,pd->iso_month,1));   // ISO reference day is 1
}
TsValue* ts_temporal_plaindate_toPlainMonthDay_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"toPlainMonthDay");
    return ts_value_make_object(TsPlainMonthDay::Create(pd->iso_month,pd->iso_day,1972));
}
TsValue* ts_temporal_plaindatetime_toPlainYearMonth_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"toPlainYearMonth");
    return ts_value_make_object(TsPlainYearMonth::Create(d->iso_year,d->iso_month,d->iso_day));
}
TsValue* ts_temporal_plaindatetime_toPlainMonthDay_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"toPlainMonthDay");
    return ts_value_make_object(TsPlainMonthDay::Create(d->iso_month,d->iso_day,1972));
}
static TsPlainYearMonth* coerce_pym_arg(TsValue* v){
    TsPlainYearMonth* p=as_plainyearmonth(v?ts_nanbox_safe_unbox(v):nullptr); if(p) return p;
    TsValue* c=ts_temporal_plainyearmonth_from(v?1:0,&v); return as_plainyearmonth(ts_nanbox_safe_unbox(c));
}
static TsValue* pym_diff(TsPlainYearMonth* a,TsPlainYearMonth* b,TsValue* opts){
    std::string smallest,largest,mode; long long inc;
    read_validated_diff_opts(opts,9,10,"month","year",&smallest,&largest,&mode,&inc);
    if(largest=="auto") largest="year";
    long long yr,mo,wk,dy;
    if((smallest=="month"||smallest=="months")&&mode=="trunc"&&inc<=1)
        diff_iso_date(a->iso_year,a->iso_month,1,b->iso_year,b->iso_month,1,largest,&yr,&mo,&wk,&dy);
    else {
        bool _re=false; round_date_duration(a->iso_year,a->iso_month,1,b->iso_year,b->iso_month,1,smallest,largest,inc,mode,&yr,&mo,&wk,&dy,&_re);
        if(_re){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: rounded date is outside the valid ISO range")); return ts_value_make_undefined(); }
    }
    return ts_value_make_object(TsDuration::Create(yr,mo,0,0,0,0,0,0,0,0));
}
TsValue* ts_temporal_plainyearmonth_until_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* a=require_plainyearmonth(ctx,"until"); TsPlainYearMonth* b=coerce_pym_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return pym_diff(a,b,(argc>=2&&argv)?argv[1]:nullptr);
}
TsValue* ts_temporal_plainyearmonth_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* a=require_plainyearmonth(ctx,"since"); TsPlainYearMonth* b=coerce_pym_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return pym_diff(b,a,(argc>=2&&argv)?argv[1]:nullptr);
}
static TsValue* pym_add_impl(TsPlainYearMonth* a,TsDuration* d,int neg){
    long long y=d->years*neg, mo=d->months*neg, wk=d->weeks*neg, dd=d->days*neg;
    int sign=(y<0||mo<0||wk<0||dd<0)?-1:1;
    int refDay=(sign<0)?iso_days_in_month(a->iso_year,a->iso_month):1;
    // The intermediate date at the reference day must itself be representable: at
    // the min year-month, day 1 (-271821-04-01) is before the minimum date, so even
    // adding a zero duration throws.
    if(!iso_date_in_limits(a->iso_year,a->iso_month,refDay)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: out of range")); return ts_value_make_undefined(); }
    int nY,nM,nD; add_iso_date(a->iso_year,a->iso_month,refDay,y,mo,wk,dd,&nY,&nM,&nD);
    if(!iso_yearmonth_in_limits(nY,nM)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainYearMonth::Create(nY,nM,1));
}
TsValue* ts_temporal_plainyearmonth_add_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* a=require_plainyearmonth(ctx,"add"); validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.add: invalid argument")); return ts_value_make_undefined(); }
    return pym_add_impl(a,d,1);
}
TsValue* ts_temporal_plainyearmonth_subtract_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* a=require_plainyearmonth(ctx,"subtract"); validate_overflow_option((argc>=2&&argv)?argv[1]:nullptr); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.subtract: invalid argument")); return ts_value_make_undefined(); }
    return pym_add_impl(a,d,-1);
}
TsValue* ts_temporal_plainyearmonth_toPlainDate_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* ym=require_plainyearmonth(ctx,"toPlainDate");
    int day=ym->iso_day;
    // toPlainDate requires an object argument carrying a day property.
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item || ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.toPlainDate: argument is required")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(!raw || (uintptr_t)raw<4096 || (uintptr_t)raw>0x00007FFFFFFFFFFFULL || *(uint32_t*)raw==0x53545247 || *(uint32_t*)raw==0x434F4E53){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.toPlainDate: argument must be an object")); return ts_value_make_undefined(); }
    TsValue* fd=ts_object_get_property(raw,"day");
    if(!fd || ts_value_is_undefined(fd)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.toPlainDate: day is required")); return ts_value_make_undefined(); }
    { double dd=ts_to_number(fd); if(std::isinf(dd)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: day property cannot be Infinity")); return ts_value_make_undefined(); } if(dd==dd) day=(int)std::trunc(dd); }
    int dim=iso_days_in_month(ym->iso_year,ym->iso_month); if(day<1)day=1; if(day>dim)day=dim;
    if(!iso_date_in_limits(ym->iso_year,ym->iso_month,day)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth.prototype.toPlainDate: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(ym->iso_year,ym->iso_month,day));
}
TsValue* ts_temporal_plainmonthday_toPlainDate_native(void* ctx,int argc,TsValue** argv){
    TsPlainMonthDay* md=require_plainmonthday(ctx,"toPlainDate");
    int year=md->iso_year;
    // toPlainDate requires an object argument carrying a year property.
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item || ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.toPlainDate: argument is required")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(!raw || (uintptr_t)raw<4096 || (uintptr_t)raw>0x00007FFFFFFFFFFFULL || *(uint32_t*)raw==0x53545247 || *(uint32_t*)raw==0x434F4E53){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.toPlainDate: argument must be an object")); return ts_value_make_undefined(); }
    TsValue* fy=ts_object_get_property(raw,"year");
    if(!fy || ts_value_is_undefined(fy)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.toPlainDate: year is required")); return ts_value_make_undefined(); }
    { double yy=ts_to_number(fy); if(std::isinf(yy)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: year property cannot be Infinity")); return ts_value_make_undefined(); } if(yy==yy) year=(int)std::trunc(yy); }
    int dim=iso_days_in_month(year,md->iso_month); int day=md->iso_day; if(day>dim)day=dim;
    if(!iso_date_in_limits(year,md->iso_month,day)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay.prototype.toPlainDate: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(year,md->iso_month,day));
}
TsValue* ts_temporal_instant_toZonedDateTimeISO_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"toZonedDateTimeISO");
    int off=0; bool utc=true;
    TsValue* tzArg=(argc>=1&&argv)?argv[0]:nullptr;
    void* tzr=tzArg?ts_nanbox_safe_unbox(tzArg):nullptr;
    if(tzr && *(uint32_t*)((char*)tzr+16)==0x5A44544D){ TsZonedDateTime* zz=(TsZonedDateTime*)tzr; off=zz->offset_minutes; utc=zz->is_utc; }
    else if(tzArg && !ts_value_is_undefined(tzArg)){
        std::string tz;
        if(!tsvalue_to_stdstring(tzArg,&tz)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","toZonedDateTimeISO: time zone must be a string")); return ts_value_make_undefined(); }
        if(!parse_timezone(tz.c_str(),&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","toZonedDateTimeISO: unsupported time zone")); return ts_value_make_undefined(); }
    }
    return ts_value_make_object(TsZonedDateTime::Create(it->epoch_ms,it->sub_ns,off,utc));
}
TsValue* ts_temporal_plaindatetime_toZonedDateTime_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"toZonedDateTime");
    require_options_object((argc>=2&&argv)?argv[1]:nullptr);
    { static const char* DIS[4]={"compatible","earlier","later","reject"};
      read_enum_option((argc>=2&&argv)?argv[1]:nullptr,"disambiguation","compatible",DIS,4); }
    int off=0; bool utc=true;
    TsValue* tzArg=(argc>=1&&argv)?argv[0]:nullptr;
    void* tzr=tzArg?ts_nanbox_safe_unbox(tzArg):nullptr;
    if(tzr && *(uint32_t*)((char*)tzr+16)==0x5A44544D){ TsZonedDateTime* zz=(TsZonedDateTime*)tzr; off=zz->offset_minutes; utc=zz->is_utc; }
    else if(tzArg && !ts_value_is_undefined(tzArg)){
        std::string tz;
        if(!tsvalue_to_stdstring(tzArg,&tz)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","toZonedDateTime: time zone must be a string")); return ts_value_make_undefined(); }
        if(!parse_timezone(tz.c_str(),&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","toZonedDateTime: unsupported time zone")); return ts_value_make_undefined(); }
    }
    long long localMs=iso_days_from_civil(d->iso_year,d->iso_month,d->iso_day)*86400000LL + (long long)d->iso_hour*3600000+(long long)d->iso_minute*60000+(long long)d->iso_second*1000+d->iso_ms;
    long long epoch_ms=localMs-(long long)off*60000LL;
    if(!instant_epoch_in_limits(epoch_ms, d->iso_us*1000+d->iso_ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.prototype.toZonedDateTime: result is outside the representable range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(epoch_ms, d->iso_us*1000+d->iso_ns, off, utc));
}
// Temporal.PlainDateTime.prototype.withPlainTime(plainTimeLike?) — keep the date,
// replace the time (default midnight when the argument is absent/undefined).
TsValue* ts_temporal_plaindatetime_withPlainTime_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"withPlainTime");
    int h=0,mi=0,s=0,ms=0,us=0,ns=0;
    TsValue* a=(argc>=1&&argv)?argv[0]:nullptr;
    if(a && !ts_value_is_undefined(a)){
        TsPlainTime* pt=coerce_plaintime_arg(a);
        if(!pt){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.withPlainTime: invalid time")); return ts_value_make_undefined(); }
        h=pt->iso_hour;mi=pt->iso_minute;s=pt->iso_second;ms=pt->iso_millisecond;us=pt->iso_microsecond;ns=pt->iso_nanosecond;
    }
    return ts_value_make_object(TsPlainDateTime::Create(d->iso_year,d->iso_month,d->iso_day,h,mi,s,ms,us,ns));
}
// Temporal.ZonedDateTime.prototype.startOfDay() — midnight in the local day.
TsValue* ts_temporal_zdt_startOfDay_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"startOfDay");
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    long long localMs=iso_days_from_civil(Y,M,D)*86400000LL;
    return ts_value_make_object(TsZonedDateTime::Create(localMs-(long long)z->offset_minutes*60000LL, 0, z->offset_minutes, z->is_utc));
}
// Temporal.PlainDate.prototype.toZonedDateTime(item) — item is a time-zone string
// or { timeZone, plainTime? }. The wall time defaults to midnight.
TsValue* ts_temporal_plaindate_toZonedDateTime_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* d=require_plaindate(ctx,"toZonedDateTime");
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    void* raw=item?ts_nanbox_safe_unbox(item):nullptr;
    if(!raw){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.toZonedDateTime: invalid argument")); return ts_value_make_undefined(); }
    int off=0; bool utc=true; int h=0,mi=0,s=0,ms=0,us=0,ns=0;
    if(*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53){
        std::string tz=((TsString*)ts_value_get_string(item))->ToUtf8();
        if(!parse_timezone(tz.c_str(),&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.toZonedDateTime: unsupported time zone")); return ts_value_make_undefined(); }
    } else {
        TsValue* tzv=ts_object_get_property(raw,"timeZone");
        std::string tz;
        if(!tzv||ts_value_is_undefined(tzv)||!tsvalue_to_stdstring(tzv,&tz)||!parse_timezone(tz.c_str(),&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.toZonedDateTime: unsupported time zone")); return ts_value_make_undefined(); }
        TsValue* ptv=ts_object_get_property(raw,"plainTime");
        if(ptv && !ts_value_is_undefined(ptv)){
            TsPlainTime* pt=coerce_plaintime_arg(ptv);
            if(pt){ h=pt->iso_hour;mi=pt->iso_minute;s=pt->iso_second;ms=pt->iso_millisecond;us=pt->iso_microsecond;ns=pt->iso_nanosecond; }
        }
    }
    long long localMs=iso_days_from_civil(d->iso_year,d->iso_month,d->iso_day)*86400000LL + (long long)h*3600000+(long long)mi*60000+(long long)s*1000+ms;
    long long epochMs=localMs-(long long)off*60000LL;
    if(!instant_epoch_in_limits(epochMs, us*1000+ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.toZonedDateTime: result is outside the representable range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(epochMs, us*1000+ns, off, utc));
}
}

// ======================= round helpers + more =======================
static bool parse_round_options(TsValue* roundTo, std::string* unit, long long* inc, std::string* mode, int minRank, int maxRank){
    *inc=1; *mode="halfExpand";
    // String shorthand: roundTo IS the smallestUnit. Validate it.
    if(roundTo && !ts_value_is_undefined(roundTo) && tsvalue_to_stdstring(roundTo, unit)){
        if(!unit_in_range(*unit,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid smallestUnit")); }
        return true;
    }
    void* raw = roundTo?ts_nanbox_safe_unbox(roundTo):nullptr; if(!raw) return false;
    // roundingMode: validate only when a string; invalid string -> RangeError.
    TsValue* rm=ts_object_get_property(raw,"roundingMode");
    if(rm&&!ts_value_is_undefined(rm)){
        std::string m;
        if(option_to_string(rm,&m)){
            if(!temporal_mode_valid(m)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingMode")); }
            *mode=m;
        }
    }
    // roundingIncrement: ToNumber; finite, then truncate(value) in [1, 1e9].
    TsValue* ri=ts_object_get_property(raw,"roundingIncrement");
    if(ri&&!ts_value_is_undefined(ri)){
        double d=(reject_nonnumeric_increment(ri), ts_to_number(ri));
        if(!(d==d)||std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingIncrement")); }
        double ii=std::trunc(d);
        if(ii<1.0||ii>1e9){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid roundingIncrement")); }
        *inc=(long long)ii;
    }
    // largestUnit (optional): validate only when a string.
    std::string luStr;
    TsValue* lu=ts_object_get_property(raw,"largestUnit");
    if(lu&&!ts_value_is_undefined(lu)){
        if(option_to_string(lu,&luStr) && luStr!="auto" && !unit_in_range(luStr,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid largestUnit")); }
    }
    // smallestUnit (required for the object form).
    TsValue* su=ts_object_get_property(raw,"smallestUnit");
    if(!su||ts_value_is_undefined(su)) return false;
    std::string s;
    if(!option_to_string(su,&s)) return false;
    if(!unit_in_range(s,minRank,maxRank)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","invalid smallestUnit")); }
    // largestUnit must be coarser than or equal to smallestUnit.
    if(!luStr.empty() && luStr!="auto" && unit_rank(luStr) < unit_rank(s)){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","largestUnit must not be smaller than smallestUnit"));
    }
    *unit=s;
    return true;
}
static long long round_nonneg(long long v, long long q, const std::string& mode){
    long long quo=v/q, r=v%q; if(r==0) return v;
    if(mode=="ceil"||mode=="expand") return (quo+1)*q;
    if(mode=="floor"||mode=="trunc") return quo*q;
    if(mode=="halfEven"){ if(r*2>q)return (quo+1)*q; if(r*2<q)return quo*q; return (quo%2==0)?quo*q:(quo+1)*q; }
    if(mode=="halfFloor"||mode=="halfTrunc") return (r*2>q)?(quo+1)*q:quo*q;
    return (r*2>=q)?(quo+1)*q:quo*q; // halfExpand / halfCeil
}
static int date_unit_rank(const std::string& u){
    if(u=="year"||u=="years")return 5; if(u=="month"||u=="months")return 4;
    if(u=="week"||u=="weeks")return 3; if(u=="day"||u=="days")return 2; return 0;
}
// Round (q + num/span) to a multiple of inc (exact integer arithmetic).
static long long round_frac(long long q,long long num,long long span,long long inc,const std::string& mode){
    if(span<=0) span=1; if(inc<=0) inc=1;
    long long val=q*span+num, step=inc*span;
    return round_nonneg(val,step,mode)/span;
}
// Round a date difference (a -> b) to smallestUnit with largestUnit balancing,
// using the spec NudgeToCalendarUnit (anchor at the earlier date, fractional
// part from the days between the truncated end and the next smallest-unit step).
static void round_date_duration(int aY,int aM,int aD,int bY,int bM,int bD,
    const std::string& smallest,const std::string& largest,long long inc,const std::string& mode,
    long long* oy,long long* omo,long long* owk,long long* ody,bool* rangeErr){
    long long ae=iso_days_from_civil(aY,aM,aD), be=iso_days_from_civil(bY,bM,bD);
    int sign=(be>=ae)?1:-1;
    int sY=aY,sM=aM,sD=aD, eY=bY,eM=bM,eD=bD;
    if(sign<0){ sY=bY;sM=bM;sD=bD; eY=aY;eM=aM;eD=aD; }   // magnitude direction (start<=end)
    // The magnitude is rounded non-negative; for a NEGATIVE duration the
    // directional modes (ceil/floor/halfCeil/halfFloor) must be flipped so the
    // re-signed result rounds the right way (e.g. ceil toward +inf = magnitude
    // toward zero). Symmetric modes (trunc/expand/halfExpand/...) are unchanged.
    std::string rmode = (sign<0) ? flip_mode_neg(mode) : mode;
    long long y,mo,wk,dy; diff_iso_date(sY,sM,sD,eY,eM,eD,largest,&y,&mo,&wk,&dy);
    long long startE=iso_days_from_civil(sY,sM,sD), endE=iso_days_from_civil(eY,eM,eD);
    long long oy_=y,omo_=mo,owk_=wk,ody_=dy;
    // A time-unit smallestUnit (e.g. the default "nanosecond" when only largestUnit is
    // given) applied to a date-only diff needs NO calendar rounding — return the raw
    // diff already balanced to largestUnit. Only date units fall through to the rounding
    // branches below (which stay byte-identical to avoid perturbing the calendar path).
    if(!(smallest=="day"||smallest=="days"||smallest=="week"||smallest=="weeks"||
         smallest=="month"||smallest=="months"||smallest=="year"||smallest=="years")){
        *oy=sign*oy_;*omo=sign*omo_;*owk=sign*owk_;*ody=sign*ody_; return;
    }
    if(smallest=="day"||smallest=="days"){
        long long totalD=endE-startE, r=round_nonneg(totalD,inc>0?inc:1,rmode);
        if(largest=="week"||largest=="weeks"){ owk_=r/7; ody_=r%7; oy_=0;omo_=0; }
        else { ody_=r; owk_=0;oy_=0;omo_=0; }
    } else if(smallest=="week"||smallest=="weeks"){
        int axY,axM,axD; add_iso_date(sY,sM,sD,y,mo,wk,0,&axY,&axM,&axD);
        int bxY,bxM,bxD; add_iso_date(axY,axM,axD,0,0,1,0,&bxY,&bxM,&bxD);
        long long nA=iso_days_from_civil(axY,axM,axD), nB=iso_days_from_civil(bxY,bxM,bxD);
        long long span=nB-nA, num=endE-nA;
        owk_=round_frac(wk,num,span,inc,rmode); oy_=y;omo_=mo;ody_=0;
    } else { // month or year
        bool isYear=(smallest=="year"||smallest=="years");
        long long q=isYear?y:mo;
        long long ty=y, tmo=isYear?0:mo;
        int axY,axM,axD; add_iso_date(sY,sM,sD,ty,tmo,0,0,&axY,&axM,&axD);
        int bxY,bxM,bxD; add_iso_date(axY,axM,axD,isYear?1:0,isYear?0:1,0,0,&bxY,&bxM,&bxD);
        long long nA=iso_days_from_civil(axY,axM,axD), nB=iso_days_from_civil(bxY,bxM,bxD);
        long long span=nB-nA, num=endE-nA;
        long long rq=round_frac(q,num,span,inc,rmode);
        if(isYear){ oy_=rq;omo_=0; } else { oy_=y;omo_=rq; }
        owk_=0;ody_=0;
        // Spec NudgeToCalendarUnit computes the date of the UPPER candidate
        // (floor-to-increment + increment) regardless of which is chosen; if that
        // date is out of range it throws. A huge increment overflows here.
        if(inc>1){ long long hiq=(q/inc)*inc+inc; int hY,hM,hD;
            if(isYear) add_iso_date(sY,sM,sD,hiq,0,0,0,&hY,&hM,&hD);
            else add_iso_date(sY,sM,sD,y,hiq,0,0,&hY,&hM,&hD);
            if(!iso_date_in_limits(hY,hM,hD)){ if(rangeErr)*rangeErr=true; *oy=*omo=*owk=*ody=0; return; } }
        // Balance the rounded month count up to years when largestUnit is year: rounding
        // can reach 12 months, which is exactly one year (e.g. {1y,11m,24d} rounded up to
        // months -> {1y,12m} -> {2y}). Use a plain carry (12 months == 1 year in the ISO
        // calendar) — re-diffing via diff_iso_date would reintroduce its end-of-month
        // borrow ambiguity (May 31 + 11 months -> April 30 reads back as 10m30d not 11m).
        if((largest=="year"||largest=="years") && (omo_>=12 || omo_<=-12)){ oy_ += omo_/12; omo_ %= 12; }
    }
    *oy=sign*oy_;*omo=sign*omo_;*owk=sign*owk_;*ody=sign*ody_;
}
static long long unit_ns(const std::string& u, bool* ok){
    *ok=true;
    if(u=="day"||u=="days") return 86400000000000LL;
    if(u=="hour"||u=="hours") return 3600000000000LL;
    if(u=="minute"||u=="minutes") return 60000000000LL;
    if(u=="second"||u=="seconds") return 1000000000LL;
    if(u=="millisecond"||u=="milliseconds") return 1000000LL;
    if(u=="microsecond"||u=="microseconds") return 1000LL;
    if(u=="nanosecond"||u=="nanoseconds") return 1LL;
    *ok=false; return 0;
}
extern "C" {
TsValue* ts_temporal_duration_round_native(void* ctx,int argc,TsValue** argv){
    TsDuration* d=require_duration(ctx,"round");
    TsValue* rt=(argc>=1&&argv)?argv[0]:nullptr;
    if(!rt||ts_value_is_undefined(rt)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Duration.prototype.round: roundTo is required")); return ts_value_make_undefined(); }
    std::string sUnit,mode="halfExpand"; long long inc=1;
    bool haveS = parse_round_options(rt,&sUnit,&inc,&mode,1,10);
    std::string lUnit="auto";
    void* raw=ts_nanbox_safe_unbox(rt);
    if(raw){ TsValue* lu=ts_object_get_property(raw,"largestUnit"); std::string s; if(lu&&!ts_value_is_undefined(lu)&&tsvalue_to_stdstring(lu,&s)) lUnit=s; }
    if(!haveS){
        // smallestUnit may be omitted iff largestUnit is given; defaults to nanosecond.
        if(lUnit=="auto"){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.round: smallestUnit or largestUnit is required")); return ts_value_make_undefined(); }
        sUnit="nanosecond";
        if(raw){ TsValue* ri=ts_object_get_property(raw,"roundingIncrement"); if(ri&&!ts_value_is_undefined(ri)){ double dd=(reject_nonnumeric_increment(ri), ts_to_number(ri)); if(dd==dd&&!std::isinf(dd))inc=(long long)std::trunc(dd); }
                 TsValue* rm=ts_object_get_property(raw,"roundingMode"); std::string m; if(rm&&!ts_value_is_undefined(rm)&&tsvalue_to_stdstring(rm,&m))mode=m; }
    }
    auto isCal=[](const std::string&u){ return u=="year"||u=="years"||u=="month"||u=="months"||u=="week"||u=="weeks"; };
    bool calInvolved = d->years||d->months||d->weeks||isCal(sUnit)||isCal(lUnit);
    if(calInvolved){
        TsValue* relTo = raw ? ts_object_get_property(raw,"relativeTo") : nullptr;
        validate_relativeto_arg(relTo);
        TsPlainDate* rd = (relTo && !ts_value_is_undefined(relTo)) ? coerce_plaindate_arg(relTo) : nullptr;
        if(!rd){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.round with calendar units requires relativeTo")); return ts_value_make_undefined(); }
        std::string L=lUnit;
        if(L=="auto"){ if(d->years)L="year"; else if(d->months)L="month"; else if(d->weeks)L="week"; else L="day"; if(date_unit_rank(L)<date_unit_rank(sUnit))L=sUnit; }
        long long extraDays=(d->hours*3600000000000LL+d->minutes*60000000000LL+d->seconds*1000000000LL+d->milliseconds*1000000LL+d->microseconds*1000LL+d->nanoseconds)/86400000000000LL;
        int ey,em,ed; add_iso_date(rd->iso_year,rd->iso_month,rd->iso_day, d->years,d->months,d->weeks, d->days+extraDays, &ey,&em,&ed);
        long long yr,mo,wk,dy; bool _re=false; round_date_duration(rd->iso_year,rd->iso_month,rd->iso_day, ey,em,ed, sUnit, L, inc, mode, &yr,&mo,&wk,&dy,&_re);
        if(_re){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal: rounded date is outside the valid ISO range")); return ts_value_make_undefined(); }
        return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy,0,0,0,0,0,0));
    }
    bool ok; long long sNs=unit_ns(sUnit,&ok); if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.round: invalid smallestUnit")); return ts_value_make_undefined(); }
    long long tot = d->days*86400000000000LL + d->hours*3600000000000LL + d->minutes*60000000000LL
        + d->seconds*1000000000LL + d->milliseconds*1000000LL + d->microseconds*1000LL + d->nanoseconds;
    long long q=sNs*(inc>0?inc:1);
    long long rounded = round_signed(tot,q,mode);
    std::string L=lUnit;
    if(L=="auto"){
        if(d->days) L="day"; else if(d->hours) L="hour"; else if(d->minutes) L="minute";
        else if(d->seconds) L="second"; else if(d->milliseconds) L="millisecond";
        else if(d->microseconds) L="microsecond"; else L="nanosecond";
        if(unit_ns(L,&ok) < sNs) L=sUnit;
    }
    long long Lns=unit_ns(L,&ok); if(!ok) Lns=86400000000000LL;
    long long rem = rounded<0?-rounded:rounded; long long rs = rounded<0?-1:1;
    long long out[7]={0,0,0,0,0,0,0}; long long uns[7]={86400000000000LL,3600000000000LL,60000000000LL,1000000000LL,1000000LL,1000LL,1LL};
    for(int i=0;i<7;i++){ if(uns[i]>=sNs && uns[i]<=Lns){ out[i]=(rem/uns[i])*rs; rem%=uns[i]; } }
    return ts_value_make_object(TsDuration::Create(0,0,0,out[0],out[1],out[2],out[3],out[4],out[5],out[6]));
}
TsValue* ts_temporal_plaindatetime_round_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* dt=require_plaindatetime(ctx,"round");
    TsValue* rt=(argc>=1&&argv)?argv[0]:nullptr;
    if(!rt||ts_value_is_undefined(rt)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.round: roundTo is required")); return ts_value_make_undefined(); }
    std::string unit,mode; long long inc;
    if(!parse_round_options(rt,&unit,&inc,&mode,1,10)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","round: smallestUnit is required")); return ts_value_make_undefined(); }
    bool ok; long long un=unit_ns(unit,&ok);
    if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","round: invalid smallestUnit")); return ts_value_make_undefined(); }
    if(inc<1)inc=1;
    validate_diff_time_increment(unit, inc);
    long long q=un*inc; long long nsOfDay=pdt_time_ns(dt);
    long long rounded=round_nonneg(nsOfDay,q,mode);
    const long long DAY=86400000000000LL; long long carry=rounded/DAY; long long rem=rounded%DAY;
    int Y,M,D; add_iso_date(dt->iso_year,dt->iso_month,dt->iso_day, 0,0,0, carry, &Y,&M,&D);
    int h=(int)(rem/3600000000000LL); rem%=3600000000000LL; int mi=(int)(rem/60000000000LL); rem%=60000000000LL;
    int s=(int)(rem/1000000000LL); rem%=1000000000LL; int ms=(int)(rem/1000000LL); rem%=1000000LL; int us=(int)(rem/1000LL); int ns=(int)(rem%1000LL);
    { long long tns=(long long)h*3600000000000LL+(long long)mi*60000000000LL+(long long)s*1000000000LL+(long long)ms*1000000LL+(long long)us*1000LL+ns;
      if(!iso_datetime_in_limits(Y,M,D,tns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime: result is outside the representable range")); return ts_value_make_undefined(); } }
    return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,h,mi,s,ms,us,ns));
}
TsValue* ts_temporal_zdt_round_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"round");
    TsValue* rt=(argc>=1&&argv)?argv[0]:nullptr;
    if(!rt||ts_value_is_undefined(rt)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.round: roundTo is required")); return ts_value_make_undefined(); }
    std::string unit,mode; long long inc;
    if(!parse_round_options(rt,&unit,&inc,&mode,1,10)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","round: smallestUnit is required")); return ts_value_make_undefined(); }
    bool ok; long long un=unit_ns(unit,&ok);
    if(!ok){ ts_throw((TsValue*)ts_error_create_typed("RangeError","round: invalid smallestUnit")); return ts_value_make_undefined(); }
    if(inc<1)inc=1;
    validate_diff_time_increment(unit, inc);
    int Y,M,D,h,mi,s,ms,us,ns; zdt_local(z,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns);
    long long nsOfDay = ((((long long)h*60+mi)*60+s)*1000000000LL)+(long long)ms*1000000LL+(long long)us*1000LL+ns;
    long long q=un*inc; long long rounded=round_nonneg(nsOfDay,q,mode);
    const long long DAY=86400000000000LL; long long carry=rounded/DAY; long long rem=rounded%DAY;
    int nY,nM,nD; add_iso_date(Y,M,D, 0,0,0, carry, &nY,&nM,&nD);
    int nh=(int)(rem/3600000000000LL); rem%=3600000000000LL; int nmi=(int)(rem/60000000000LL); rem%=60000000000LL;
    int nss=(int)(rem/1000000000LL); rem%=1000000000LL; int nms=(int)(rem/1000000LL); rem%=1000000LL; int nus=(int)(rem/1000LL); int nns=(int)(rem%1000LL);
    long long localMs=iso_days_from_civil(nY,nM,nD)*86400000LL+(long long)nh*3600000+(long long)nmi*60000+(long long)nss*1000+nms;
    long long epoch_ms=localMs-(long long)z->offset_minutes*60000LL;
    if(!instant_epoch_in_limits(epoch_ms, nus*1000+nns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime.prototype.round: result is outside the representable range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(epoch_ms, nus*1000+nns, z->offset_minutes, z->is_utc));
}
}

// Time-only difference -> Duration, honoring largestUnit + smallestUnit rounding.
static TsValue* duration_from_time_opts(long long diff, const std::string& largest, long long smallestNs, const std::string& mode){
    int sign=diff<0?-1:1; long long ad=diff<0?-diff:diff;
    if(smallestNs>1) ad = round_nonneg(ad, smallestNs, sign<0?flip_mode_neg(mode):mode);
    long long h=0,mi=0,s=0,ms=0,us=0,ns=0; long long rem=ad;
    std::string L = largest.empty()?"hour":largest;
    if(L=="hour"||L=="hours"){ h=rem/3600000000000LL; rem%=3600000000000LL; mi=rem/60000000000LL; rem%=60000000000LL; s=rem/1000000000LL; rem%=1000000000LL; ms=rem/1000000LL; rem%=1000000LL; us=rem/1000LL; ns=rem%1000LL; }
    else if(L=="minute"||L=="minutes"){ mi=rem/60000000000LL; rem%=60000000000LL; s=rem/1000000000LL; rem%=1000000000LL; ms=rem/1000000LL; rem%=1000000LL; us=rem/1000LL; ns=rem%1000LL; }
    else if(L=="second"||L=="seconds"){ s=rem/1000000000LL; rem%=1000000000LL; ms=rem/1000000LL; rem%=1000000LL; us=rem/1000LL; ns=rem%1000LL; }
    else if(L=="millisecond"||L=="milliseconds"){ ms=rem/1000000LL; rem%=1000000LL; us=rem/1000LL; ns=rem%1000LL; }
    else if(L=="microsecond"||L=="microseconds"){ us=rem/1000LL; ns=rem%1000LL; }
    else if(L=="nanosecond"||L=="nanoseconds"){ ns=rem; }
    else { h=rem/3600000000000LL; rem%=3600000000000LL; mi=rem/60000000000LL; rem%=60000000000LL; s=rem/1000000000LL; rem%=1000000000LL; ms=rem/1000000LL; rem%=1000000LL; us=rem/1000LL; ns=rem%1000LL; }
    return ts_value_make_object(TsDuration::Create(0,0,0,0, sign*h, sign*mi, sign*s, sign*ms, sign*us, sign*ns));
}
// Read largestUnit / smallestUnit / roundingMode for a time-diff and produce the Duration.
static TsValue* time_diff_with_opts(long long diff, TsValue* opts, const char* defLargest){
    std::string largest,smallest,mode; long long inc;
    read_validated_diff_opts(opts,1,6,"nanosecond",defLargest,&smallest,&largest,&mode,&inc);
    validate_diff_time_increment(smallest, inc);
    if(largest=="auto") largest=defLargest;
    bool ok; long long sNs = unit_ns(smallest, &ok); if(!ok) sNs=1;
    if(inc<1)inc=1;
    return duration_from_time_opts(diff, largest, sNs*inc, mode);
}
