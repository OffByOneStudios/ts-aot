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

TsValue* ts_temporal_plaintime_toString_native(void* ctx, int argc, TsValue** argv) {
    TsPlainTime* pt = require_plaintime(ctx, "toString");
    return ts_value_make_string(plaintime_iso_string(pt));
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
    const char* u = sp ? ((TsString*)sp)->ToUtf8() : nullptr;
    if (!u) return false;
    *out = u; return true;
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
            double d = ts_to_number(ri);
            if (d == d && !std::isinf(d)) increment = (long)std::trunc(d);
        }
        TsValue* rm = ts_object_get_property(raw, "roundingMode");
        std::string m; if (rm && tsvalue_to_stdstring(rm, &m)) mode = m;
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
    TsValue* arg = (argc >= 1 && argv) ? argv[0] : nullptr;
    void* raw = arg ? ts_nanbox_safe_unbox(arg) : nullptr;
    if (!raw) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError",
            "Temporal.PlainTime.prototype.with: argument must be an object"));
        return ts_value_make_undefined();
    }
    uint32_t m0 = *(uint32_t*)raw;
    if (m0 == 0x53545247 || m0 == 0x434F4E53 ||
        *(uint32_t*)((char*)raw + 16) == TsPlainTime::MAGIC) {
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

// Parse an ISO-8601 time (optionally preceded by a date + 'T'). Fills the six
// fields. Returns false on malformed input or a UTC 'Z' designator (a bare
// PlainTime string must not carry a zone). Lenient on separators.
static bool parse_iso_time(const char* s, int* H, int* M, int* S,
                           int* ms, int* us, int* ns) {
    const char* t = s;
    for (const char* p = s; *p; p++) { if (*p == 'T' || *p == 't') { t = p + 1; break; } }
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
    *H = h; *M = m; *S = sec;
    *ms = (int)(frac / 1000000); *us = (int)((frac / 1000) % 1000); *ns = (int)(frac % 1000);
    return true;
}

extern "C" TsValue* ts_temporal_plaintime_from(int argc, TsValue** argv) {
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
            H < 0 || H > 23 || M < 0 || M > 59 || S < 0 || S > 59) {
            ts_throw((TsValue*)ts_error_create_typed("RangeError",
                "Temporal.PlainTime.from: string is not a valid ISO time"));
            return ts_value_make_undefined();
        }
        return ts_value_make_object(TsPlainTime::Create(H, M, S, ms, us, ns));
    }
    // Property bag: read recognized fields (default overflow "constrain" clamps).
    if (raw) {
        static const char* names[6] = {"hour","minute","second","millisecond","microsecond","nanosecond"};
        int vals[6] = {0,0,0,0,0,0};
        const int lim[6] = {23,59,59,999,999,999};
        bool any = false;
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
                int v = (int)std::trunc(d);
                if (v < 0) v = 0; if (v > lim[i]) v = lim[i];
                vals[i] = v;
            }
        }
        if (!any) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Temporal.PlainTime.from: object has no recognized time fields"));
            return ts_value_make_undefined();
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

extern "C" TsValue* ts_temporal_duration_construct(int argc, TsValue** argv) {
    long long f[10]; bool ok = true;
    for (int i = 0; i < 10; i++) {
        f[i] = duration_field((i < argc && argv) ? argv[i] : nullptr, &ok);
        if (!ok) return ts_value_make_undefined();
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
static TsString* duration_iso_string(TsDuration* d) {
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
    bool anyTime = d->hours||d->minutes||d->seconds||d->milliseconds||d->microseconds||d->nanoseconds;
    if (anyTime) {
        out += "T";
        if (d->hours)   { snprintf(b,sizeof(b),"%lldH",u(d->hours));   out += b; }
        if (d->minutes) { snprintf(b,sizeof(b),"%lldM",u(d->minutes)); out += b; }
        long long frac = u(d->milliseconds)*1000000LL + u(d->microseconds)*1000LL + u(d->nanoseconds);
        if (d->seconds || frac) {
            snprintf(b,sizeof(b),"%lld",u(d->seconds)); out += b;
            if (frac) {
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
    return ts_value_make_string(duration_iso_string(d));
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
        if (*p=='T'||*p=='t') { inTime=true; p++; continue; }
        if (!isdigit((unsigned char)*p)) return false;
        long long val=0; while(isdigit((unsigned char)*p)){ val=val*10+(*p-'0'); p++; }
        long long fracNs=0; bool hasFrac=false;
        if (*p=='.'||*p==',') { hasFrac=true; p++; char fb[10]="000000000"; int i=0; while(i<9&&isdigit((unsigned char)*p)){fb[i++]=*p++;} while(isdigit((unsigned char)*p))p++; fracNs=atol(fb); }
        char unit=*p; if(!unit) return false; p++;
        anyField=true;
        if (!inTime) {
            if (hasFrac) return false;
            switch(unit){ case 'Y':case 'y':f[0]=sign*val;break; case 'M':case 'm':f[1]=sign*val;break;
                case 'W':case 'w':f[2]=sign*val;break; case 'D':case 'd':f[3]=sign*val;break; default:return false; }
        } else {
            switch(unit){
                case 'H':case 'h': f[4]=sign*val; break;
                case 'M':case 'm': f[5]=sign*val; break;
                case 'S':case 's':
                    f[6]=sign*val;
                    if (fracNs){ f[7]=sign*(fracNs/1000000); f[8]=sign*((fracNs/1000)%1000); f[9]=sign*(fracNs%1000); }
                    break;
                default: return false;
            }
        }
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
    if (!iso_date_valid(y, m, d)) {
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate: date out of range"));
        return ts_value_make_undefined();
    }
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
static bool parse_iso_date(const char* s, int* Y, int* M, int* D) {
    int sign = 1; const char* p = s;
    if (*p=='+'||*p=='-') { if(*p=='-') sign=-1; p++; }
    int y=0, nd=0;
    while (isdigit((unsigned char)*p)) { y=y*10+(*p-'0'); p++; nd++; }
    if (nd < 4) return false;
    if (*p=='-') p++;
    if (!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    if (*p=='-') p++;
    if (!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int da=(p[0]-'0')*10+(p[1]-'0'); p+=2;
    *Y=sign*y; *M=mo; *D=da;
    return true;
}

extern "C" {

TsValue* ts_temporal_plaindate_toString_native(void* ctx, int argc, TsValue** argv) {
    TsPlainDate* d = require_plaindate(ctx, "toString");
    return ts_value_make_string(plaindate_iso_string(d));
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
    TsPlainDate* b = as_plaindate(other?ts_nanbox_safe_unbox(other):nullptr);
    if (!b) {
        TsValue* c = ts_temporal_plaindate_from(other?1:0, &other);
        b = as_plaindate(ts_nanbox_safe_unbox(c));
        if (!b) return ts_value_make_bool(false);
    }
    return ts_value_make_bool(a->iso_year==b->iso_year && a->iso_month==b->iso_month && a->iso_day==b->iso_day);
}

TsValue* ts_temporal_plaindate_compare_native(void* ctx, int argc, TsValue** argv) {
    (void)ctx;
    auto toPD = [](TsValue* v) -> TsPlainDate* {
        TsPlainDate* p = as_plaindate(v?ts_nanbox_safe_unbox(v):nullptr);
        if (p) return p;
        TsValue* c = ts_temporal_plaindate_from(v?1:0, &v);
        return as_plaindate(ts_nanbox_safe_unbox(c));
    };
    TsPlainDate* a = toPD((argc>=1)?argv[0]:nullptr);
    TsPlainDate* b = toPD((argc>=2)?argv[1]:nullptr);
    if (!a || !b) return ts_value_make_int(0);
    int af[3]={a->iso_year,a->iso_month,a->iso_day}, bf[3]={b->iso_year,b->iso_month,b->iso_day};
    for (int i=0;i<3;i++){ if(af[i]<bf[i]) return ts_value_make_int(-1); if(af[i]>bf[i]) return ts_value_make_int(1); }
    return ts_value_make_int(0);
}

TsValue* ts_temporal_plaindate_with_native(void* ctx, int argc, TsValue** argv) {
    TsPlainDate* pd = require_plaindate(ctx, "with");
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
    TsValue* item = (argc>=1&&argv)?argv[0]:nullptr;
    if (!item || ts_value_is_undefined(item)) {
        ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.from: argument is undefined"));
        return ts_value_make_undefined();
    }
    void* raw = ts_nanbox_safe_unbox(item);
    if (raw) {
        uint32_t m0 = *(uint32_t*)raw;
        if (m0==0x53545247 || m0==0x434F4E53) {
            const char* utf = ((TsString*)ts_value_get_string(item))->ToUtf8();
            int Y,M,D;
            if (!utf || !parse_iso_date(utf,&Y,&M,&D) || !iso_date_valid(Y,M,D)) {
                ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.from: invalid ISO date string"));
                return ts_value_make_undefined();
            }
            return ts_value_make_object(TsPlainDate::Create(Y,M,D));
        }
        if (*(uint32_t*)((char*)raw+16)==TsPlainDate::MAGIC) {
            TsPlainDate* o = (TsPlainDate*)raw;
            return ts_value_make_object(TsPlainDate::Create(o->iso_year,o->iso_month,o->iso_day));
        }
        // property bag: year/month(or monthCode)/day all required.
        TsValue* fy = ts_object_get_property(raw,"year");
        TsValue* fm = ts_object_get_property(raw,"month");
        TsValue* fd = ts_object_get_property(raw,"day");
        bool hasY = fy && !ts_value_is_undefined(fy);
        bool hasM = fm && !ts_value_is_undefined(fm);
        bool hasD = fd && !ts_value_is_undefined(fd);
        if (!hasY || !hasM || !hasD) {
            ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.from: object needs year, month and day"));
            return ts_value_make_undefined();
        }
        double dy=ts_to_number(fy), dm=ts_to_number(fm), dd=ts_to_number(fd);
        if (dy!=dy||dm!=dm||dd!=dd){ ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite")); return ts_value_make_undefined(); }
        int Y=(int)std::trunc(dy), M=(int)std::trunc(dm), D=(int)std::trunc(dd);
        // from default overflow constrain
        if (M<1) M=1; if (M>12) M=12;
        int dim=iso_days_in_month(Y,M); if (D<1) D=1; if (D>dim) D=dim;
        if (!iso_date_valid(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","date out of range")); return ts_value_make_undefined(); }
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
    int refDay=fld(3,&ord); if(!ord) refDay=1;
    if(!oy||!om){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: year and month are required")); return ts_value_make_undefined(); }
    if(m<1||m>12||!iso_date_valid(y,m,refDay)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth: out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainYearMonth::Create(y,m,refDay));
}
static bool parse_iso_yearmonth(const char* s, int* Y, int* M) {
    int sign=1; const char* p=s; if(*p=='+'||*p=='-'){if(*p=='-')sign=-1;p++;}
    int y=0,nd=0; while(isdigit((unsigned char)*p)){y=y*10+(*p-'0');p++;nd++;} if(nd<4) return false;
    if(*p=='-')p++; if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); *Y=sign*y; *M=mo; return true;
}
extern "C" {
TsValue* ts_temporal_plainyearmonth_toString_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* d=require_plainyearmonth(ctx,"toString"); char b[24];
    if(d->iso_year<0||d->iso_year>9999) snprintf(b,sizeof(b),"%+07d-%02d",d->iso_year,d->iso_month);
    else snprintf(b,sizeof(b),"%04d-%02d",d->iso_year,d->iso_month);
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
    TsPlainYearMonth* pd=require_plainyearmonth(ctx,"with"); TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr; void* raw=arg?ts_nanbox_safe_unbox(arg):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||*(uint32_t*)((char*)raw+16)==TsPlainYearMonth::MAGIC){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    int vals[2]={pd->iso_year,pd->iso_month}; static const char* names[2]={"year","month"}; bool any=false;
    for(int i=0;i<2;i++){ TsValue* f=ts_object_get_property(raw,names[i]); if(f&&!ts_value_is_undefined(f)){any=true; double d=ts_to_number(f); if(d!=d||std::isinf(d)){ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite"));return ts_value_make_undefined();} vals[i]=(int)std::trunc(d);} }
    if(!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if(vals[1]<1)vals[1]=1; if(vals[1]>12)vals[1]=12;
    return ts_value_make_object(TsPlainYearMonth::Create(vals[0],vals[1],pd->iso_day));
}
TsValue* ts_temporal_plainyearmonth_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_plainyearmonth_from(argc,argv); }
}
extern "C" TsValue* ts_temporal_plainyearmonth_from(int argc, TsValue** argv){
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ const char* u=((TsString*)ts_value_get_string(item))->ToUtf8(); int Y,M; if(!u||!parse_iso_yearmonth(u,&Y,&M)||M<1||M>12||!iso_date_valid(Y,M,1)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainYearMonth.from: invalid string")); return ts_value_make_undefined(); } return ts_value_make_object(TsPlainYearMonth::Create(Y,M,1)); }
        if(*(uint32_t*)((char*)raw+16)==TsPlainYearMonth::MAGIC){ TsPlainYearMonth* o=(TsPlainYearMonth*)raw; return ts_value_make_object(TsPlainYearMonth::Create(o->iso_year,o->iso_month,o->iso_day)); }
        TsValue* fy=ts_object_get_property(raw,"year"); TsValue* fm=ts_object_get_property(raw,"month");
        bool hY=fy&&!ts_value_is_undefined(fy),hM=fm&&!ts_value_is_undefined(fm);
        if(!hY||!hM){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainYearMonth.from: object needs year and month")); return ts_value_make_undefined(); }
        double dy=ts_to_number(fy),dm=ts_to_number(fm); if(dy!=dy||dm!=dm){ ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite")); return ts_value_make_undefined(); }
        int Y=(int)std::trunc(dy),M=(int)std::trunc(dm); if(M<1)M=1; if(M>12)M=12;
        if(!iso_date_valid(Y,M,1)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","out of range")); return ts_value_make_undefined(); }
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
    bool om=true,od=true,ory=true; int m=fld(0,&om),d=fld(1,&od); int refY=fld(3,&ory); if(!ory) refY=1972;
    if(!om||!od){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay: month and day are required")); return ts_value_make_undefined(); }
    if(m<1||m>12||d<1||d>iso_days_in_month(refY,m)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay: out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainMonthDay::Create(m,d,refY));
}
static bool parse_iso_monthday(const char* s, int* M, int* D) {
    const char* p=s; if(p[0]=='-'&&p[1]=='-') p+=2;
    // could be MM-DD or YYYY-MM-DD; if 4+ leading digits treat as date.
    int lead=0; const char* q=p; while(isdigit((unsigned char)*q)){lead++;q++;}
    if(lead>=4){ int yy,mm,dd; if(!parse_iso_date(s,&yy,&mm,&dd)) return false; *M=mm;*D=dd;return true; }
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int mo=(p[0]-'0')*10+(p[1]-'0'); p+=2; if(*p=='-')p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int da=(p[0]-'0')*10+(p[1]-'0'); *M=mo; *D=da; return true;
}
extern "C" {
TsValue* ts_temporal_plainmonthday_toString_native(void* ctx,int argc,TsValue** argv){
    TsPlainMonthDay* d=require_plainmonthday(ctx,"toString"); char b[16]; snprintf(b,sizeof(b),"%02d-%02d",d->iso_month,d->iso_day);
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
    TsPlainMonthDay* pd=require_plainmonthday(ctx,"with"); TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr; void* raw=arg?ts_nanbox_safe_unbox(arg):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||*(uint32_t*)((char*)raw+16)==TsPlainMonthDay::MAGIC){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    int M=pd->iso_month,D=pd->iso_day; bool any=false;
    TsValue* fm=ts_object_get_property(raw,"month"); TsValue* fd=ts_object_get_property(raw,"day");
    if(fm&&!ts_value_is_undefined(fm)){any=true; double d=ts_to_number(fm); if(d==d&&!std::isinf(d))M=(int)std::trunc(d);}
    if(fd&&!ts_value_is_undefined(fd)){any=true; double d=ts_to_number(fd); if(d==d&&!std::isinf(d))D=(int)std::trunc(d);}
    if(!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(pd->iso_year,M); if(D<1)D=1; if(D>dim)D=dim;
    return ts_value_make_object(TsPlainMonthDay::Create(M,D,pd->iso_year));
}
TsValue* ts_temporal_plainmonthday_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_plainmonthday_from(argc,argv); }
}
extern "C" TsValue* ts_temporal_plainmonthday_from(int argc, TsValue** argv){
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ const char* u=((TsString*)ts_value_get_string(item))->ToUtf8(); int M,D; if(!u||!parse_iso_monthday(u,&M,&D)||M<1||M>12||D<1||D>iso_days_in_month(1972,M)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainMonthDay.from: invalid string")); return ts_value_make_undefined(); } return ts_value_make_object(TsPlainMonthDay::Create(M,D,1972)); }
        if(*(uint32_t*)((char*)raw+16)==TsPlainMonthDay::MAGIC){ TsPlainMonthDay* o=(TsPlainMonthDay*)raw; return ts_value_make_object(TsPlainMonthDay::Create(o->iso_month,o->iso_day,o->iso_year)); }
        TsValue* fm=ts_object_get_property(raw,"month"); TsValue* fd=ts_object_get_property(raw,"day");
        bool hM=fm&&!ts_value_is_undefined(fm),hD=fd&&!ts_value_is_undefined(fd);
        if(!hM||!hD){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainMonthDay.from: object needs month and day")); return ts_value_make_undefined(); }
        double dm=ts_to_number(fm),dd=ts_to_number(fd); if(dm!=dm||dd!=dd){ ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite")); return ts_value_make_undefined(); }
        int M=(int)std::trunc(dm),D=(int)std::trunc(dd); if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(1972,M); if(D<1)D=1; if(D>dim)D=dim;
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
    if(!iso_date_valid(y,mo,d)||!pdt_time_valid(h,mi,s,ms,us,ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime: out of range")); return ts_value_make_undefined(); }
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
static bool parse_iso_datetime(const char* s,int* Y,int* M,int* D,int* H,int* Mi,int* S,int* ms,int* us,int* ns){
    if(!parse_iso_date(s,Y,M,D)) return false;
    *H=0;*Mi=0;*S=0;*ms=0;*us=0;*ns=0;
    const char* p=s; while(*p&&*p!='T'&&*p!='t'&&*p!=' ') p++;
    if(!*p) return true;  // date-only is valid for PlainDateTime.from
    p++;
    auto two=[](const char* q,int* o)->const char*{ if(!isdigit((unsigned char)q[0])||!isdigit((unsigned char)q[1]))return nullptr; *o=(q[0]-'0')*10+(q[1]-'0'); return q+2; };
    const char* q=two(p,H); if(!q) return false; if(*q==':')q++;
    if(isdigit((unsigned char)q[0])&&isdigit((unsigned char)q[1])){ q=two(q,Mi); if(*q==':')q++;
        if(isdigit((unsigned char)q[0])&&isdigit((unsigned char)q[1])){ q=two(q,S);
            if(*q=='.'||*q==','){ q++; char fb[10]="000000000"; int i=0; while(i<9&&isdigit((unsigned char)*q)){fb[i++]=*q++;} long f=atol(fb); *ms=(int)(f/1000000);*us=(int)((f/1000)%1000);*ns=(int)(f%1000);} } }
    return true;
}
extern "C" {
TsValue* ts_temporal_plaindatetime_toString_native(void* ctx,int argc,TsValue** argv){ return ts_value_make_string(plaindatetime_iso_string(require_plaindatetime(ctx,"toString"))); }
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
    TsPlainDateTime* pd=require_plaindatetime(ctx,"with"); TsValue* arg=(argc>=1&&argv)?argv[0]:nullptr; void* raw=arg?ts_nanbox_safe_unbox(arg):nullptr;
    if(!raw||*(uint32_t*)raw==0x53545247||*(uint32_t*)raw==0x434F4E53||*(uint32_t*)((char*)raw+16)==TsPlainDateTime::MAGIC){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.with: argument must be a plain object")); return ts_value_make_undefined(); }
    static const char* names[9]={"year","month","day","hour","minute","second","millisecond","microsecond","nanosecond"};
    int vals[9]={pd->iso_year,pd->iso_month,pd->iso_day,pd->iso_hour,pd->iso_minute,pd->iso_second,pd->iso_ms,pd->iso_us,pd->iso_ns};
    bool any=false;
    for(int i=0;i<9;i++){ TsValue* f=ts_object_get_property(raw,names[i]); if(f&&!ts_value_is_undefined(f)){any=true; double d=ts_to_number(f); if(d!=d||std::isinf(d)){ts_throw((TsValue*)ts_error_create_typed("RangeError","field not finite"));return ts_value_make_undefined();} vals[i]=(int)std::trunc(d);} }
    if(!any){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.with: no recognized fields")); return ts_value_make_undefined(); }
    if(vals[1]<1)vals[1]=1; if(vals[1]>12)vals[1]=12; int dim=iso_days_in_month(vals[0],vals[1]); if(vals[2]<1)vals[2]=1; if(vals[2]>dim)vals[2]=dim;
    int* T=vals+3; const int lim[6]={23,59,59,999,999,999}; for(int i=0;i<6;i++){ if(T[i]<0)T[i]=0; if(T[i]>lim[i])T[i]=lim[i]; }
    return ts_value_make_object(TsPlainDateTime::Create(vals[0],vals[1],vals[2],vals[3],vals[4],vals[5],vals[6],vals[7],vals[8]));
}
TsValue* ts_temporal_plaindatetime_toPlainDate_native(void* ctx,int argc,TsValue** argv){ TsPlainDateTime* d=require_plaindatetime(ctx,"toPlainDate"); return ts_value_make_object(TsPlainDate::Create(d->iso_year,d->iso_month,d->iso_day)); }
TsValue* ts_temporal_plaindatetime_toPlainTime_native(void* ctx,int argc,TsValue** argv){ TsPlainDateTime* d=require_plaindatetime(ctx,"toPlainTime"); return ts_value_make_object(TsPlainTime::Create(d->iso_hour,d->iso_minute,d->iso_second,d->iso_ms,d->iso_us,d->iso_ns)); }
TsValue* ts_temporal_plaindatetime_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_plaindatetime_from(argc,argv); }
}
extern "C" TsValue* ts_temporal_plaindatetime_from(int argc, TsValue** argv){
    TsValue* item=(argc>=1&&argv)?argv[0]:nullptr;
    if(!item||ts_value_is_undefined(item)){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.from: argument is undefined")); return ts_value_make_undefined(); }
    void* raw=ts_nanbox_safe_unbox(item);
    if(raw){
        uint32_t m0=*(uint32_t*)raw;
        if(m0==0x53545247||m0==0x434F4E53){ const char* u=((TsString*)ts_value_get_string(item))->ToUtf8(); int Y,M,D,H,Mi,S,ms,us,ns;
            if(!u||!parse_iso_datetime(u,&Y,&M,&D,&H,&Mi,&S,&ms,&us,&ns)||!iso_date_valid(Y,M,D)||!pdt_time_valid(H,Mi,S,ms,us,ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime.from: invalid string")); return ts_value_make_undefined(); }
            return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,H,Mi,S,ms,us,ns)); }
        if(*(uint32_t*)((char*)raw+16)==TsPlainDateTime::MAGIC){ TsPlainDateTime* o=(TsPlainDateTime*)raw; return ts_value_make_object(TsPlainDateTime::Create(o->iso_year,o->iso_month,o->iso_day,o->iso_hour,o->iso_minute,o->iso_second,o->iso_ms,o->iso_us,o->iso_ns)); }
        TsValue* fy=ts_object_get_property(raw,"year"),*fm=ts_object_get_property(raw,"month"),*fd=ts_object_get_property(raw,"day");
        bool hY=fy&&!ts_value_is_undefined(fy),hM=fm&&!ts_value_is_undefined(fm),hD=fd&&!ts_value_is_undefined(fd);
        if(!hY||!hM||!hD){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.from: object needs year, month and day")); return ts_value_make_undefined(); }
        auto rd=[&](const char* k,int def)->int{ TsValue* f=ts_object_get_property(raw,k); if(!f||ts_value_is_undefined(f))return def; double d=ts_to_number(f); if(d!=d||std::isinf(d))return def; return (int)std::trunc(d); };
        int Y=rd("year",0),M=rd("month",1),D=rd("day",1),H=rd("hour",0),Mi=rd("minute",0),S=rd("second",0),ms=rd("millisecond",0),us=rd("microsecond",0),ns=rd("nanosecond",0);
        if(M<1)M=1; if(M>12)M=12; int dim=iso_days_in_month(Y,M); if(D<1)D=1; if(D>dim)D=dim;
        const int lim[6]={23,59,59,999,999,999}; int* tp[6]={&H,&Mi,&S,&ms,&us,&ns}; for(int i=0;i<6;i++){ if(*tp[i]<0)*tp[i]=0; if(*tp[i]>lim[i])*tp[i]=lim[i]; }
        if(!iso_date_valid(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","out of range")); return ts_value_make_undefined(); }
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
extern "C" {
TsValue* ts_temporal_now_plaindatetimeiso_native(void* ctx,int argc,TsValue** argv){
    int Y,M,D,h,m,s,ms,us,ns; temporal_now_fields(&Y,&M,&D,&h,&m,&s,&ms,&us,&ns);
    return ts_value_make_object(TsPlainDateTime::Create(Y,M,D,h,m,s,ms,us,ns));
}
TsValue* ts_temporal_now_plaindateiso_native(void* ctx,int argc,TsValue** argv){
    int Y,M,D,h,m,s,ms,us,ns; temporal_now_fields(&Y,&M,&D,&h,&m,&s,&ms,&us,&ns);
    return ts_value_make_object(TsPlainDate::Create(Y,M,D));
}
TsValue* ts_temporal_now_plaintimeiso_native(void* ctx,int argc,TsValue** argv){
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
// epoch nanoseconds as a decimal string (sign + |ms| + 6-digit |sub_ns|).
static void instant_ns_string(TsInstant* it, char* buf, size_t n){
    const char* sign = (it->epoch_ms < 0 || it->sub_ns < 0) ? "-" : "";
    long long ams = it->epoch_ms < 0 ? -it->epoch_ms : it->epoch_ms;
    int asub = it->sub_ns < 0 ? -it->sub_ns : it->sub_ns;
    snprintf(buf, n, "%s%lld%06d", sign, ams, asub);
}
TsValue TsInstant::GetPropertyVirtual(const char* key){
    if(strcmp(key,"epochMilliseconds")==0){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=epoch_ms; return r; }
    if(strcmp(key,"epochSeconds")==0){ TsValue r; r.type=ValueType::NUMBER_INT; r.i_val=epoch_ms/1000; return r; }
    if(strcmp(key,"epochNanoseconds")==0){ char b[40]; instant_ns_string(this,b,sizeof(b)); TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_str((void*)TsString::Create(b),10); return r; }
    if(strcmp(key,"epochMicroseconds")==0){ TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_int(epoch_ms*1000LL + sub_ns/1000); return r; }
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
    if(!u || !ns_string_to_ms_sub(u,&ms,&sub)){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant: epochNanoseconds out of range"));
        return ts_value_make_undefined();
    }
    return ts_value_make_object(TsInstant::Create(ms,sub));
}
static TsString* instant_iso_string(TsInstant* it){
    long long ms=it->epoch_ms;
    long long days = ms / 86400000LL; long long rem = ms % 86400000LL;
    if(rem < 0){ rem += 86400000LL; days -= 1; }
    int Y,M,D; iso_civil_from_days(days,&Y,&M,&D);
    int h=(int)(rem/3600000); rem%=3600000; int mi=(int)(rem/60000); rem%=60000; int s=(int)(rem/1000); int msr=(int)(rem%1000);
    long frac=(long)msr*1000000L + (it->sub_ns<0?-it->sub_ns:it->sub_ns); // ns within second
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
TsValue* ts_temporal_instant_toString_native(void* ctx,int argc,TsValue** argv){ return ts_value_make_string(instant_iso_string(require_instant(ctx,"toString"))); }
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
    (void)ctx; double d=(argc>=1&&argv&&argv[0])?ts_to_number(argv[0]):0; if(d!=d||std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fromEpochMilliseconds: not finite")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create((long long)d,0));
}
TsValue* ts_temporal_instant_fromEpochSec_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; double d=(argc>=1&&argv&&argv[0])?ts_to_number(argv[0]):0; if(d!=d||std::isinf(d)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","fromEpochSeconds: not finite")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsInstant::Create((long long)d*1000LL,0));
}
TsValue* ts_temporal_instant_from_native(void* ctx,int argc,TsValue** argv){ (void)ctx; return ts_temporal_instant_from(argc,argv); }
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
            if(!u || !parse_iso_datetime(u,&Y,&M,&D,&h,&mi,&s,&ms,&us,&ns)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.from: invalid string")); return ts_value_make_undefined(); }
            long long days=iso_days_from_civil(Y,M,D);
            long long epoch_ms = days*86400000LL + (long long)h*3600000LL + (long long)mi*60000LL + (long long)s*1000LL + ms;
            int subNs = us*1000 + ns;
            // NOTE: an explicit numeric offset in the string is not yet applied (treated as UTC).
            return ts_value_make_object(TsInstant::Create(epoch_ms, subNs));
        }
    }
    ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.from: invalid argument")); return ts_value_make_undefined();
}

// ====================== Temporal.ZonedDateTime ======================
TsZonedDateTime* TsZonedDateTime::Create(long long ms, int subNs, int offMin, bool utc){
    void* mem=ts_alloc(sizeof(TsZonedDateTime)); TsZonedDateTime* o=new(mem) TsZonedDateTime();
    o->magic=MAGIC; o->epoch_ms=ms; o->sub_ns=subNs; o->offset_minutes=offMin; o->is_utc=utc; return o;
}
// Local wall-clock breakdown (epoch + fixed offset).
static void zdt_local(TsZonedDateTime* z,int* Y,int* M,int* D,int* h,int* mi,int* s,int* ms,int* us,int* ns){
    long long local = z->epoch_ms + (long long)z->offset_minutes*60000LL;
    long long days = local/86400000LL; long long rem = local%86400000LL;
    if(rem<0){ rem+=86400000LL; days-=1; }
    iso_civil_from_days(days,Y,M,D);
    *h=(int)(rem/3600000); rem%=3600000; *mi=(int)(rem/60000); rem%=60000; *s=(int)(rem/1000); *ms=(int)(rem%1000);
    int an = z->sub_ns<0?-z->sub_ns:z->sub_ns; *us=an/1000; *ns=an%1000;
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
    if(strcmp(key,"epochNanoseconds")==0){ const char* sign=(epoch_ms<0||sub_ns<0)?"-":""; long long ams=epoch_ms<0?-epoch_ms:epoch_ms; int asub=sub_ns<0?-sub_ns:sub_ns; char b[40]; snprintf(b,sizeof(b),"%s%lld%06d",sign,ams,asub); TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_str((void*)TsString::Create(b),10); return r; }
    if(strcmp(key,"epochMicroseconds")==0){ TsValue r; r.type=ValueType::BIGINT_PTR; r.ptr_val=ts_bigint_create_int(epoch_ms*1000LL+sub_ns/1000); return r; }
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
static bool parse_timezone(const char* s, int* offMin, bool* isUtc){
    if(!s) return false;
    if(strcmp(s,"UTC")==0||strcmp(s,"utc")==0){ *offMin=0; *isUtc=true; return true; }
    const char* p=s; if(*p=='Z'||*p=='z'){ *offMin=0; *isUtc=true; return true; }
    int sign=0; if(*p=='+')sign=1; else if(*p=='-')sign=-1; else return false; p++;
    if(!isdigit((unsigned char)p[0])||!isdigit((unsigned char)p[1])) return false;
    int hh=(p[0]-'0')*10+(p[1]-'0'); p+=2; if(*p==':')p++;
    int mm=0; if(isdigit((unsigned char)p[0])&&isdigit((unsigned char)p[1])) mm=(p[0]-'0')*10+(p[1]-'0');
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
    if(!parse_timezone(tz,&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.ZonedDateTime: unsupported time zone (only UTC and numeric offsets)")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsZonedDateTime::Create(ms,sub,off,utc));
}
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
TsValue* ts_temporal_zdt_epochNs_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"epochNanoseconds"); TsValue v=z->GetPropertyVirtual("epochNanoseconds"); return (TsValue*)v.ptr_val; }
TsValue* ts_temporal_zdt_epochMicros_native(void* ctx,int argc,TsValue** argv){ TsZonedDateTime* z=require_zoneddatetime(ctx,"epochMicroseconds"); TsValue v=z->GetPropertyVirtual("epochMicroseconds"); return (TsValue*)v.ptr_val; }
TsValue* ts_temporal_zdt_toString_native(void* ctx,int argc,TsValue** argv){ return ts_value_make_string(zdt_iso_string(require_zoneddatetime(ctx,"toString"))); }
TsValue* ts_temporal_zdt_valueOf_native(void* ctx,int argc,TsValue** argv){ (void)ctx; ts_throw((TsValue*)ts_error_create_typed("TypeError","Called valueOf on a Temporal.ZonedDateTime; use compare() or equals() instead")); return ts_value_make_undefined(); }
TsValue* ts_temporal_zdt_equals_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* a=require_zoneddatetime(ctx,"equals"); TsValue* o=(argc>=1&&argv)?argv[0]:nullptr;
    TsZonedDateTime* b=as_zoneddatetime(o?ts_nanbox_safe_unbox(o):nullptr); if(!b) return ts_value_make_bool(false);
    return ts_value_make_bool(a->epoch_ms==b->epoch_ms&&a->sub_ns==b->sub_ns&&a->offset_minutes==b->offset_minutes&&a->is_utc==b->is_utc);
}
TsValue* ts_temporal_zdt_compare_native(void* ctx,int argc,TsValue** argv){
    (void)ctx; TsZonedDateTime* a=as_zoneddatetime((argc>=1&&argv)?ts_nanbox_safe_unbox(argv[0]):nullptr);
    TsZonedDateTime* b=as_zoneddatetime((argc>=2&&argv)?ts_nanbox_safe_unbox(argv[1]):nullptr);
    if(!a||!b) return ts_value_make_int(0);
    if(a->epoch_ms!=b->epoch_ms) return ts_value_make_int(a->epoch_ms<b->epoch_ms?-1:1);
    if(a->sub_ns!=b->sub_ns) return ts_value_make_int(a->sub_ns<b->sub_ns?-1:1);
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
    return duration_from_time_ns(pt_to_ns(b) - pt_to_ns(a));
}
TsValue* ts_temporal_plaintime_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainTime* a=require_plaintime(ctx,"since"); TsPlainTime* b=coerce_plaintime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainTime.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return duration_from_time_ns(pt_to_ns(a) - pt_to_ns(b));
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
    if(!opts) return def;
    void* raw = ts_nanbox_safe_unbox(opts);
    if(!raw) return def;
    uint32_t m0=*(uint32_t*)raw; if(m0==0x53545247||m0==0x434F4E53) return def;
    TsValue* v = ts_object_get_property(raw, key);
    std::string s;
    if(v && !ts_value_is_undefined(v) && tsvalue_to_stdstring(v,&s)){ if(s=="auto") return def; return s; }
    return def;
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
    TsPlainDate* p = as_plaindate(v?ts_nanbox_safe_unbox(v):nullptr);
    if(p) return p;
    TsValue* c = ts_temporal_plaindate_from(v?1:0,&v);
    return as_plaindate(ts_nanbox_safe_unbox(c));
}
extern "C" {
TsValue* ts_temporal_plaindate_add_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"add"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    int Y,M,D; add_iso_date(pd->iso_year,pd->iso_month,pd->iso_day, d->years,d->months,d->weeks,d->days,&Y,&M,&D);
    if(!iso_date_valid(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.add: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(Y,M,D));
}
TsValue* ts_temporal_plaindate_subtract_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"subtract"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    int Y,M,D; add_iso_date(pd->iso_year,pd->iso_month,pd->iso_day, -d->years,-d->months,-d->weeks,-d->days,&Y,&M,&D);
    if(!iso_date_valid(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDate.prototype.subtract: result out of range")); return ts_value_make_undefined(); }
    return ts_value_make_object(TsPlainDate::Create(Y,M,D));
}
TsValue* ts_temporal_plaindate_until_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* a=require_plaindate(ctx,"until"); TsPlainDate* b=coerce_plaindate_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    std::string lu = read_string_option((argc>=2&&argv)?argv[1]:nullptr, "largestUnit", "day");
    long long yr,mo,wk,dy; diff_iso_date(a->iso_year,a->iso_month,a->iso_day,b->iso_year,b->iso_month,b->iso_day,lu,&yr,&mo,&wk,&dy);
    return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy,0,0,0,0,0,0));
}
TsValue* ts_temporal_plaindate_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* a=require_plaindate(ctx,"since"); TsPlainDate* b=coerce_plaindate_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDate.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    std::string lu = read_string_option((argc>=2&&argv)?argv[1]:nullptr, "largestUnit", "day");
    long long yr,mo,wk,dy; diff_iso_date(b->iso_year,b->iso_month,b->iso_day,a->iso_year,a->iso_month,a->iso_day,lu,&yr,&mo,&wk,&dy);
    return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy,0,0,0,0,0,0));
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
    if(!iso_date_valid(Y,M,D)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.PlainDateTime arithmetic: result out of range")); return ts_value_make_undefined(); }
    int h=(int)(rem/3600000000000LL); rem%=3600000000000LL; int mi=(int)(rem/60000000000LL); rem%=60000000000LL;
    int s=(int)(rem/1000000000LL); rem%=1000000000LL; int ms=(int)(rem/1000000LL); rem%=1000000LL; int us=(int)(rem/1000LL); int ns=(int)(rem%1000LL);
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
    if(lu=="day"||lu=="days"||lu=="hour"||lu=="hours"||lu=="minute"||lu=="minutes"||lu=="second"||lu=="seconds"||lu=="millisecond"||lu=="milliseconds"||lu=="microsecond"||lu=="microseconds"||lu=="nanosecond"||lu=="nanoseconds"){
        return ts_value_make_object(TsDuration::Create(0,0,0, days, tsign*h, tsign*mi, tsign*s, tsign*ms, tsign*us, tsign*ns));
    }
    // year/month/week: convert the day count to calendar units (adjusted end date).
    long long acivil = iso_days_from_civil(a->iso_year,a->iso_month,a->iso_day);
    int ey,em,ed; iso_civil_from_days(acivil+days, &ey,&em,&ed);
    long long yr,mo,wk,dy; diff_iso_date(a->iso_year,a->iso_month,a->iso_day, ey,em,ed, lu, &yr,&mo,&wk,&dy);
    return ts_value_make_object(TsDuration::Create(yr,mo,wk,dy, tsign*h, tsign*mi, tsign*s, tsign*ms, tsign*us, tsign*ns));
}
extern "C" {
TsValue* ts_temporal_plaindatetime_add_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* dt=require_plaindatetime(ctx,"add"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    return pdt_add(dt,d,1);
}
TsValue* ts_temporal_plaindatetime_subtract_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* dt=require_plaindatetime(ctx,"subtract"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    return pdt_add(dt,d,-1);
}
TsValue* ts_temporal_plaindatetime_until_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* a=require_plaindatetime(ctx,"until"); TsPlainDateTime* b=coerce_plaindatetime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return pdt_diff(a,b, read_string_option((argc>=2&&argv)?argv[1]:nullptr,"largestUnit","day"));
}
TsValue* ts_temporal_plaindatetime_since_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* a=require_plaindatetime(ctx,"since"); TsPlainDateTime* b=coerce_plaindatetime_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.PlainDateTime.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return pdt_diff(b,a, read_string_option((argc>=2&&argv)?argv[1]:nullptr,"largestUnit","day"));
}
}

// ======================= Arithmetic: Instant =======================
// Build a time-only Duration from a sign-aligned (ms, sub-ns) magnitude.
static TsValue* duration_from_ms_sub(long long ms, long long subNs, const std::string& largest){
    int sign = (ms<0||subNs<0)?-1:1; long long ams=ms<0?-ms:ms; long long asub=subNs<0?-subNs:subNs;
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
    return ts_value_make_object(TsInstant::Create(oms,osub));
}
TsValue* ts_temporal_instant_subtract_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"subtract"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    if(d->years||d->months||d->weeks||d->days){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Instant.prototype.subtract: duration must be time-only")); return ts_value_make_undefined(); }
    long long oms; int osub; instant_add_time(it->epoch_ms,it->sub_ns,d,-1,&oms,&osub);
    return ts_value_make_object(TsInstant::Create(oms,osub));
}
TsValue* ts_temporal_instant_until_native(void* ctx,int argc,TsValue** argv){
    TsInstant* a=require_instant(ctx,"until"); TsInstant* b=coerce_instant_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    long long ms=b->epoch_ms-a->epoch_ms; long long sub=(long long)b->sub_ns-a->sub_ns;
    if(ms>0&&sub<0){ms--;sub+=1000000;} else if(ms<0&&sub>0){ms++;sub-=1000000;}
    return duration_from_ms_sub(ms,sub, read_string_option((argc>=2&&argv)?argv[1]:nullptr,"largestUnit","second"));
}
TsValue* ts_temporal_instant_since_native(void* ctx,int argc,TsValue** argv){
    TsInstant* a=require_instant(ctx,"since"); TsInstant* b=coerce_instant_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.Instant.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    long long ms=a->epoch_ms-b->epoch_ms; long long sub=(long long)a->sub_ns-b->sub_ns;
    if(ms>0&&sub<0){ms--;sub+=1000000;} else if(ms<0&&sub>0){ms++;sub-=1000000;}
    return duration_from_ms_sub(ms,sub, read_string_option((argc>=2&&argv)?argv[1]:nullptr,"largestUnit","second"));
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
    return ts_value_make_object(TsDuration::Create(0,0,0,days, tsign*hh,tsign*mm,tsign*ss,tsign*mms,tsign*uus,tsign*nns));
}
extern "C" {
TsValue* ts_temporal_zdt_add_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"add"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.add: invalid duration")); return ts_value_make_undefined(); }
    return zdt_add(z,d,1);
}
TsValue* ts_temporal_zdt_subtract_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* z=require_zoneddatetime(ctx,"subtract"); TsDuration* d=coerce_duration_arg((argc>=1&&argv)?argv[0]:nullptr);
    if(!d){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.subtract: invalid duration")); return ts_value_make_undefined(); }
    return zdt_add(z,d,-1);
}
TsValue* ts_temporal_zdt_until_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* a=require_zoneddatetime(ctx,"until"); TsZonedDateTime* b=as_zoneddatetime((argc>=1&&argv)?ts_nanbox_safe_unbox(argv[0]):nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.until: invalid argument")); return ts_value_make_undefined(); }
    return zdt_diff(a,b, read_string_option((argc>=2&&argv)?argv[1]:nullptr,"largestUnit","hour"));
}
TsValue* ts_temporal_zdt_since_native(void* ctx,int argc,TsValue** argv){
    TsZonedDateTime* a=require_zoneddatetime(ctx,"since"); TsZonedDateTime* b=as_zoneddatetime((argc>=1&&argv)?ts_nanbox_safe_unbox(argv[0]):nullptr);
    if(!b){ ts_throw((TsValue*)ts_error_create_typed("TypeError","Temporal.ZonedDateTime.prototype.since: invalid argument")); return ts_value_make_undefined(); }
    return zdt_diff(b,a, read_string_option((argc>=2&&argv)?argv[1]:nullptr,"largestUnit","hour"));
}
}

// ====================== Arithmetic: Duration ======================
static TsValue* duration_add_impl(TsDuration* a, TsDuration* b, int bsign){
    if(a->years||a->months||a->weeks||b->years||b->months||b->weeks){
        ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration arithmetic with calendar units requires relativeTo"));
        return ts_value_make_undefined();
    }
    const long long DAY=86400000000000LL;
    long long totalDays = a->days + bsign*b->days;
    long long ns = dur_time_ns(a) + bsign*dur_time_ns(b);
    totalDays += ns/DAY; ns %= DAY;
    if(totalDays>0 && ns<0){ totalDays--; ns+=DAY; } else if(totalDays<0 && ns>0){ totalDays++; ns-=DAY; }
    int sign=(totalDays<0||ns<0)?-1:1; long long ad=totalDays<0?-totalDays:totalDays; long long an=ns<0?-ns:ns;
    long long h=an/3600000000000LL; an%=3600000000000LL; long long mi=an/60000000000LL; an%=60000000000LL;
    long long s=an/1000000000LL; an%=1000000000LL; long long ms=an/1000000LL; an%=1000000LL; long long us=an/1000LL; long long nn=an%1000LL;
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
    if(d->years||d->months||d->weeks){ ts_throw((TsValue*)ts_error_create_typed("RangeError","Temporal.Duration.prototype.total with calendar units requires relativeTo")); return ts_value_make_undefined(); }
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
    return ts_value_make_object(TsPlainDateTime::Create(pd->iso_year,pd->iso_month,pd->iso_day,h,mi,s,ms,us,ns));
}
TsValue* ts_temporal_plaindate_toPlainYearMonth_native(void* ctx,int argc,TsValue** argv){
    TsPlainDate* pd=require_plaindate(ctx,"toPlainYearMonth");
    return ts_value_make_object(TsPlainYearMonth::Create(pd->iso_year,pd->iso_month,pd->iso_day));
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
TsValue* ts_temporal_plainyearmonth_toPlainDate_native(void* ctx,int argc,TsValue** argv){
    TsPlainYearMonth* ym=require_plainyearmonth(ctx,"toPlainDate");
    int day=ym->iso_day;
    if(argc>=1&&argv&&argv[0]){ void* raw=ts_nanbox_safe_unbox(argv[0]); if(raw){ TsValue* fd=ts_object_get_property(raw,"day"); if(fd&&!ts_value_is_undefined(fd)){ double dd=ts_to_number(fd); if(dd==dd&&!std::isinf(dd)) day=(int)std::trunc(dd); } } }
    int dim=iso_days_in_month(ym->iso_year,ym->iso_month); if(day<1)day=1; if(day>dim)day=dim;
    return ts_value_make_object(TsPlainDate::Create(ym->iso_year,ym->iso_month,day));
}
TsValue* ts_temporal_plainmonthday_toPlainDate_native(void* ctx,int argc,TsValue** argv){
    TsPlainMonthDay* md=require_plainmonthday(ctx,"toPlainDate");
    int year=md->iso_year;
    if(argc>=1&&argv&&argv[0]){ void* raw=ts_nanbox_safe_unbox(argv[0]); if(raw){ TsValue* fy=ts_object_get_property(raw,"year"); if(fy&&!ts_value_is_undefined(fy)){ double yy=ts_to_number(fy); if(yy==yy&&!std::isinf(yy)) year=(int)std::trunc(yy); } } }
    int dim=iso_days_in_month(year,md->iso_month); int day=md->iso_day; if(day>dim)day=dim;
    return ts_value_make_object(TsPlainDate::Create(year,md->iso_month,day));
}
TsValue* ts_temporal_instant_toZonedDateTimeISO_native(void* ctx,int argc,TsValue** argv){
    TsInstant* it=require_instant(ctx,"toZonedDateTimeISO");
    int off=0; bool utc=true;
    if(argc>=1&&argv&&argv[0]){ std::string tz; if(tsvalue_to_stdstring(argv[0],&tz)){ if(!parse_timezone(tz.c_str(),&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","toZonedDateTimeISO: unsupported time zone")); return ts_value_make_undefined(); } } }
    return ts_value_make_object(TsZonedDateTime::Create(it->epoch_ms,it->sub_ns,off,utc));
}
TsValue* ts_temporal_plaindatetime_toZonedDateTime_native(void* ctx,int argc,TsValue** argv){
    TsPlainDateTime* d=require_plaindatetime(ctx,"toZonedDateTime");
    int off=0; bool utc=true;
    if(argc>=1&&argv&&argv[0]){ std::string tz; if(tsvalue_to_stdstring(argv[0],&tz)){ if(!parse_timezone(tz.c_str(),&off,&utc)){ ts_throw((TsValue*)ts_error_create_typed("RangeError","toZonedDateTime: unsupported time zone")); return ts_value_make_undefined(); } } }
    long long localMs=iso_days_from_civil(d->iso_year,d->iso_month,d->iso_day)*86400000LL + (long long)d->iso_hour*3600000+(long long)d->iso_minute*60000+(long long)d->iso_second*1000+d->iso_ms;
    long long epoch_ms=localMs-(long long)off*60000LL;
    return ts_value_make_object(TsZonedDateTime::Create(epoch_ms, d->iso_us*1000+d->iso_ns, off, utc));
}
}
