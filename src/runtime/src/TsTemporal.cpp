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
