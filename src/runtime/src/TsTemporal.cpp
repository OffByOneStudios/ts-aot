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
