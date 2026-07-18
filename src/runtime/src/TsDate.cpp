#include "TsDate.h"
#include "TsString.h"
#include "TsNanBox.h"
#include "TsRuntime.h"
#include "TsError.h"
#include "GC.h"
#include <chrono>
#include <cmath>
#include <new>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unicode/calendar.h>
#include <unicode/gregocal.h>
#include <unicode/timezone.h>
#include <unicode/smpdtfmt.h>
#include <unicode/datefmt.h>

// ---------------------------------------------------------------------------
// Civil-date helpers (Howard Hinnant's algorithms). ts-aot treats the local
// timezone as UTC (Date.prototype.getTimezoneOffset returns 0), so all Date
// field math and string serialization operate on UTC. These helpers handle
// negative and extended (>4-digit) years, which ICU's calendar/format path
// mishandles for the ECMA-262 ISO 8601 string forms.
// ---------------------------------------------------------------------------
namespace {
    // Days since 1970-01-01 for a proleptic Gregorian date. y is the actual
    // year, m in [1,12], d in [1,31]. Result can be negative.
    static int64_t days_from_civil(int64_t y, int64_t m, int64_t d) {
        y -= (m <= 2);
        int64_t era = (y >= 0 ? y : y - 399) / 400;
        int64_t yoe = y - era * 400;                           // [0, 399]
        int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;  // [0, 365]
        int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;   // [0, 146096]
        return era * 146097 + doe - 719468;
    }
    // Inverse: proleptic Gregorian date from days since 1970-01-01.
    static void civil_from_days(int64_t z, int64_t& y, int64_t& m, int64_t& d) {
        z += 719468;
        int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        int64_t doe = z - era * 146097;                        // [0, 146096]
        int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0,399]
        y = yoe + era * 400;
        int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100); // [0, 365]
        int64_t mp = (5 * doy + 2) / 153;                      // [0, 11]
        d = doy - (153 * mp + 2) / 5 + 1;                      // [1, 31]
        m = mp + (mp < 10 ? 3 : -9);                           // [1, 12]
        y += (m <= 2);
    }

    static const char* kWeekdays[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* kMonths[12]  = {"Jan","Feb","Mar","Apr","May","Jun",
                                       "Jul","Aug","Sep","Oct","Nov","Dec"};

    struct DateFields {
        int64_t year, month, day, hours, minutes, seconds, millis, weekday;
    };
    // Break UTC ms into broken-down fields using floor division so that times
    // before the epoch produce correct (non-negative) sub-day components.
    static DateFields fields_from_ms(int64_t ms) {
        const int64_t MS_PER_DAY = 86400000LL;
        int64_t days = ms / MS_PER_DAY;
        int64_t rem  = ms % MS_PER_DAY;
        if (rem < 0) { rem += MS_PER_DAY; days -= 1; }
        int64_t y, mo, d;
        civil_from_days(days, y, mo, d);
        DateFields f;
        f.year = y; f.month = mo - 1; f.day = d;
        f.hours   = rem / 3600000; rem %= 3600000;
        f.minutes = rem / 60000;   rem %= 60000;
        f.seconds = rem / 1000;
        f.millis  = rem % 1000;
        int64_t wd = (days + 4) % 7;   // 1970-01-01 was a Thursday (index 4)
        if (wd < 0) wd += 7;
        f.weekday = wd;
        return f;
    }

    // ECMA-262 21.4.4.41.4 year serialization for toString/toUTCString/etc.:
    // signed, zero-padded to at least four digits.
    static std::string fmt_year_min4(int64_t y) {
        char buf[24];
        if (y < 0) snprintf(buf, sizeof buf, "-%04lld", (long long)(-y));
        else       snprintf(buf, sizeof buf, "%04lld",  (long long)y);
        return buf;
    }
    // ECMA-262 21.4.4.36 toISOString year: 4 digits for [0,9999], else 6-digit
    // with an explicit sign.
    static std::string fmt_year_iso(int64_t y) {
        char buf[24];
        if (y >= 0 && y <= 9999) snprintf(buf, sizeof buf, "%04lld", (long long)y);
        else if (y < 0)          snprintf(buf, sizeof buf, "-%06lld", (long long)(-y));
        else                     snprintf(buf, sizeof buf, "+%06lld", (long long)y);
        return buf;
    }

    // TimeClip (ECMA-262 21.4.1.31): non-finite or |t| > 8.64e15 -> Invalid.
    static int64_t time_clip(double t) {
        if (!std::isfinite(t)) return TsDate::INVALID;
        if (t < -8.64e15 || t > 8.64e15) return TsDate::INVALID;
        return (int64_t)std::trunc(t);
    }

    // MakeDay/MakeTime/MakeDate (ECMA-262 21.4.1.11-13) on already-integral,
    // finite components. Month/day out of range carry per spec.
    static double make_date_ms(double year, double month, double day,
                               double hour, double minute, double second,
                               double milli) {
        // MakeDay
        double ym = year + std::floor(month / 12.0);
        double mn = month - std::floor(month / 12.0) * 12.0;  // [0,11]
        int64_t base = days_from_civil((int64_t)ym, (int64_t)mn + 1, 1);
        double day_num = (double)base + (day - 1.0);
        // MakeTime
        double time = ((hour * 60.0 + minute) * 60.0 + second) * 1000.0 + milli;
        return day_num * 86400000.0 + time;
    }
}


TsDate* TsDate::Create() {
    void* mem = ts_alloc(sizeof(TsDate));
    return new(mem) TsDate();
}

TsDate* TsDate::Create(int64_t milliseconds) {
    void* mem = ts_alloc(sizeof(TsDate));
    return new(mem) TsDate(milliseconds);
}

TsDate* TsDate::CreateFromParts(double y, double mo, double d,
                                double h, double mi, double s, double ms) {
    // ECMA-262 §21.4.2.1: any non-finite component (NaN or ±Infinity) makes an
    // Invalid Date (MakeDay/MakeTime yield NaN, TimeClip -> NaN).
    if (!std::isfinite(y) || !std::isfinite(mo) || !std::isfinite(d) ||
        !std::isfinite(h) || !std::isfinite(mi) || !std::isfinite(s) ||
        !std::isfinite(ms)) {
        return Create(INVALID);
    }
    // ToIntegerOrInfinity truncates toward zero, then year 0-99 maps to
    // 1900-1999 (spec §21.4.2.1 step 3d MakeFullYear).
    double year = std::trunc(y);
    if (year >= 0 && year <= 99) year += 1900;
    double newMs = make_date_ms(year, std::trunc(mo), std::trunc(d),
                                std::trunc(h), std::trunc(mi), std::trunc(s),
                                std::trunc(ms));
    return Create(time_clip(newMs));
}

// ECMA-262 21.4.3.2 Date Time String Format:
//   [+-YYYYYY|YYYY]  (-MM)?  (-DD)?   ( T HH:mm (:ss)? (.sss)? (Z|±HH:mm)? )?
// Date-only forms are UTC; a date-time form without a timezone offset is local
// time, which for ts-aot equals UTC. Returns true and sets *out on success.
static bool parse_iso_date(const char* s, int64_t* out) {
    if (!s) return false;
    const char* p = s;
    auto skipws = [&]() { while (*p == ' ' || *p == '\t') ++p; };
    skipws();

    // ---- year (sign => 6 digits, else 4 digits) ----
    int sign = 1;
    bool signed_year = false;
    if (*p == '+' || *p == '-') { signed_year = true; if (*p == '-') sign = -1; ++p; }
    int ydigits = signed_year ? 6 : 4;
    int64_t year = 0;
    for (int i = 0; i < ydigits; ++i) {
        if (*p < '0' || *p > '9') return false;
        year = year * 10 + (*p - '0');
        ++p;
    }
    year *= sign;
    // Per spec, "-000000" (negative zero year) is invalid.
    if (signed_year && sign < 0 && year == 0) return false;

    int64_t month = 1, day = 1;
    bool have_time = false;
    int64_t hour = 0, minute = 0, second = 0, milli = 0;
    bool have_tz = false; int64_t tz_min = 0;  // minutes to subtract to reach UTC

    auto two = [&](int64_t* v) -> bool {
        if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
        *v = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2; return true;
    };

    if (*p == '-') {
        ++p;
        if (!two(&month)) return false;
        if (*p == '-') { ++p; if (!two(&day)) return false; }
    }

    if (*p == 'T') {
        ++p;
        have_time = true;
        if (!two(&hour)) return false;
        if (*p != ':') return false;
        ++p;
        if (!two(&minute)) return false;
        if (*p == ':') { ++p; if (!two(&second)) return false; }
        if (*p == '.') {
            ++p;
            // Exactly three fractional digits per the spec grammar.
            if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' ||
                p[2] < '0' || p[2] > '9') return false;
            milli = (p[0]-'0')*100 + (p[1]-'0')*10 + (p[2]-'0');
            p += 3;
        }
        if (*p == 'Z') { have_tz = true; tz_min = 0; ++p; }
        else if (*p == '+' || *p == '-') {
            int tzsign = (*p == '-') ? -1 : 1; ++p;
            int64_t th = 0, tm = 0;
            if (!two(&th)) return false;
            if (*p == ':') ++p;   // colon is optional per common usage
            if (!two(&tm)) return false;
            if (th > 23 || tm > 59) return false;
            tz_min = tzsign * (th * 60 + tm);
            have_tz = true;
        }
    }
    (void)have_tz;

    skipws();
    if (*p != '\0') return false;   // trailing garbage -> not this format

    // Range validation (ECMA-262 21.4.3.2 rejects out-of-range components).
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    // 24:00:00.000 is the only allowed hour==24 value.
    if (hour > 24 || (hour == 24 && (minute || second || milli))) return false;
    if (minute > 59 || second > 59) return false;

    double ms = make_date_ms((double)year, (double)(month - 1), (double)day,
                             (double)hour, (double)minute, (double)second,
                             (double)milli);
    ms -= (double)tz_min * 60000.0;
    int64_t clipped = time_clip(ms);
    if (clipped == TsDate::INVALID) return false;
    *out = clipped;
    (void)have_time;
    return true;
}

TsDate* TsDate::Create(const char* dateStr) {
    int64_t isoMs = 0;
    if (parse_iso_date(dateStr, &isoMs)) {
        return Create(isoMs);
    }
    // Fall back to ICU for non-ISO forms (e.g. "Mon Jan 01 1970 ...").
    UErrorCode status = U_ZERO_ERROR;
    icu::SimpleDateFormat fmt(icu::UnicodeString("yyyy-MM-dd'T'HH:mm:ss.SSSX"), status);
    fmt.setTimeZone(*icu::TimeZone::getGMT());
    UDate date = fmt.parse(icu::UnicodeString(dateStr), status);
    if (U_FAILURE(status)) {
        return Create(INVALID);
    }
    return Create(time_clip((double)date));
}

bool TsDate::IsValid() const {
    return ms != INVALID;
}

TsDate::TsDate() {
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

TsDate::TsDate(int64_t milliseconds) : ms(milliseconds) {}

int64_t TsDate::GetTime() {
    return ms;
}

// All getters operate on UTC (ts-aot's local timezone == UTC). Local and UTC
// variants therefore return identical values, matching getTimezoneOffset()==0.
int64_t TsDate::GetFullYear() {
    if (ms == INVALID) return INVALID;
    return fields_from_ms(ms).year;
}
int64_t TsDate::GetMonth() { return ms == INVALID ? INVALID : fields_from_ms(ms).month; }
int64_t TsDate::GetDate() { return ms == INVALID ? INVALID : fields_from_ms(ms).day; }
int64_t TsDate::GetHours() { return ms == INVALID ? INVALID : fields_from_ms(ms).hours; }
int64_t TsDate::GetMinutes() { return ms == INVALID ? INVALID : fields_from_ms(ms).minutes; }
int64_t TsDate::GetSeconds() { return ms == INVALID ? INVALID : fields_from_ms(ms).seconds; }
int64_t TsDate::GetMilliseconds() { return ms == INVALID ? INVALID : fields_from_ms(ms).millis; }

int64_t TsDate::GetUTCFullYear() { return GetFullYear(); }
int64_t TsDate::GetUTCMonth() { return GetMonth(); }
int64_t TsDate::GetUTCDate() { return GetDate(); }
int64_t TsDate::GetUTCHours() { return GetHours(); }
int64_t TsDate::GetUTCMinutes() { return GetMinutes(); }
int64_t TsDate::GetUTCSeconds() { return GetSeconds(); }
int64_t TsDate::GetUTCMilliseconds() { return GetMilliseconds(); }

// All setters interpret components as UTC (local == UTC). Each replaces one
// broken-down field and recomputes the time value via MakeDate + TimeClip.
// An Invalid receiver stays invalid (the kept components are NaN).
void TsDate::SetFullYear(int64_t year) {
    DateFields f = fields_from_ms(ms == INVALID ? 0 : ms);  // setFullYear revives epoch
    ms = time_clip(make_date_ms((double)year, (double)f.month, (double)f.day,
                                (double)f.hours, (double)f.minutes,
                                (double)f.seconds, (double)f.millis));
}
void TsDate::SetMonth(int64_t month) {
    if (ms == INVALID) return;
    DateFields f = fields_from_ms(ms);
    ms = time_clip(make_date_ms((double)f.year, (double)month, (double)f.day,
                                (double)f.hours, (double)f.minutes,
                                (double)f.seconds, (double)f.millis));
}
void TsDate::SetDate(int64_t date) {
    if (ms == INVALID) return;
    DateFields f = fields_from_ms(ms);
    ms = time_clip(make_date_ms((double)f.year, (double)f.month, (double)date,
                                (double)f.hours, (double)f.minutes,
                                (double)f.seconds, (double)f.millis));
}
void TsDate::SetHours(int64_t hours) {
    if (ms == INVALID) return;
    DateFields f = fields_from_ms(ms);
    ms = time_clip(make_date_ms((double)f.year, (double)f.month, (double)f.day,
                                (double)hours, (double)f.minutes,
                                (double)f.seconds, (double)f.millis));
}
void TsDate::SetMinutes(int64_t minutes) {
    if (ms == INVALID) return;
    DateFields f = fields_from_ms(ms);
    ms = time_clip(make_date_ms((double)f.year, (double)f.month, (double)f.day,
                                (double)f.hours, (double)minutes,
                                (double)f.seconds, (double)f.millis));
}
void TsDate::SetSeconds(int64_t seconds) {
    if (ms == INVALID) return;
    DateFields f = fields_from_ms(ms);
    ms = time_clip(make_date_ms((double)f.year, (double)f.month, (double)f.day,
                                (double)f.hours, (double)f.minutes,
                                (double)seconds, (double)f.millis));
}
void TsDate::SetMilliseconds(int64_t milliseconds) {
    if (ms == INVALID) return;
    DateFields f = fields_from_ms(ms);
    ms = time_clip(make_date_ms((double)f.year, (double)f.month, (double)f.day,
                                (double)f.hours, (double)f.minutes,
                                (double)f.seconds, (double)milliseconds));
}
void TsDate::SetUTCFullYear(int64_t year)     { SetFullYear(year); }
void TsDate::SetUTCMonth(int64_t month)       { SetMonth(month); }
void TsDate::SetUTCDate(int64_t date)         { SetDate(date); }
void TsDate::SetUTCHours(int64_t hours)       { SetHours(hours); }
void TsDate::SetUTCMinutes(int64_t minutes)   { SetMinutes(minutes); }
void TsDate::SetUTCSeconds(int64_t seconds)   { SetSeconds(seconds); }
void TsDate::SetUTCMilliseconds(int64_t m)    { SetMilliseconds(m); }

double TsDate::SetFields(bool utc, int64_t baseMs, double year, double month,
                         double date, double hour, double minute, double second,
                         double milli, bool revive) {
    (void)utc;  // local == UTC in ts-aot
    int64_t base;
    if (baseMs != INVALID) {
        base = baseMs;
    } else if (revive) {
        base = 0;  // setFullYear treats an Invalid Date as the epoch (+0)
    } else {
        // Any other setter on an Invalid Date leaves it invalid (a kept-current
        // component derived from NaN is NaN → result NaN).
        ms = INVALID;
        return std::nan("");
    }
    DateFields f = fields_from_ms(base);
    double y  = std::isnan(year)   ? (double)f.year    : year;
    double mo = std::isnan(month)  ? (double)f.month   : month;
    double dt = std::isnan(date)   ? (double)f.day     : date;
    double h  = std::isnan(hour)   ? (double)f.hours   : hour;
    double mi = std::isnan(minute) ? (double)f.minutes : minute;
    double s  = std::isnan(second) ? (double)f.seconds : second;
    double ml = std::isnan(milli)  ? (double)f.millis  : milli;
    int64_t clipped = time_clip(make_date_ms(y, mo, dt, h, mi, s, ml));
    ms = clipped;
    if (clipped == INVALID) return std::nan("");
    return (double)ms;
}

TsString* TsDate::ToISOString() {
    // Callers (ts_date_toISOString_native) already threw RangeError for an
    // Invalid Date, but guard anyway.
    if (ms == INVALID) return TsString::Create("Invalid Date");
    DateFields f = fields_from_ms(ms);
    char buf[64];
    snprintf(buf, sizeof buf, "%s-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ",
             fmt_year_iso(f.year).c_str(),
             (long long)(f.month + 1), (long long)f.day,
             (long long)f.hours, (long long)f.minutes,
             (long long)f.seconds, (long long)f.millis);
    return TsString::Create(buf);
}

TsString* TsDate::ToJSON() {
    // toJSON returns the same as toISOString for Date objects
    return ToISOString();
}

// ECMA-262 21.4.4.41.1 DateString(t): "Www Mmm DD YYYY".
static std::string dateString(const DateFields& f) {
    char buf[64];
    snprintf(buf, sizeof buf, "%s %s %02lld %s",
             kWeekdays[f.weekday], kMonths[f.month], (long long)f.day,
             fmt_year_min4(f.year).c_str());
    return buf;
}
// ECMA-262 21.4.4.41.2 TimeString(t): "HH:mm:ss GMT".
static std::string timeString(const DateFields& f) {
    char buf[32];
    snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld GMT",
             (long long)f.hours, (long long)f.minutes, (long long)f.seconds);
    return buf;
}

TsString* TsDate::ToString() {
    if (ms == INVALID) return TsString::Create("Invalid Date");
    DateFields f = fields_from_ms(ms);
    // DateString " " TimeString TimeZoneString. TimeZoneString is
    // "+0000 (Coordinated Universal Time)" (offset 0 in ts-aot).
    std::string s = dateString(f) + " " + timeString(f) +
                    "+0000 (Coordinated Universal Time)";
    return TsString::Create(s.c_str());
}

TsString* TsDate::ToTimeString() {
    if (ms == INVALID) return TsString::Create("Invalid Date");
    DateFields f = fields_from_ms(ms);
    std::string s = timeString(f) + "+0000 (Coordinated Universal Time)";
    return TsString::Create(s.c_str());
}

TsString* TsDate::ToUTCString() {
    // ECMA-262 21.4.4.43: "Www, DD Mmm YYYY HH:mm:ss GMT".
    if (ms == INVALID) return TsString::Create("Invalid Date");
    DateFields f = fields_from_ms(ms);
    char buf[64];
    snprintf(buf, sizeof buf, "%s, %02lld %s %s %02lld:%02lld:%02lld GMT",
             kWeekdays[f.weekday], (long long)f.day, kMonths[f.month],
             fmt_year_min4(f.year).c_str(),
             (long long)f.hours, (long long)f.minutes, (long long)f.seconds);
    return TsString::Create(buf);
}

TsString* TsDate::ToDateString() {
    if (ms == INVALID) return TsString::Create("Invalid Date");
    return TsString::Create(dateString(fields_from_ms(ms)).c_str());
}

int64_t TsDate::Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

extern "C" {
    void* ts_date_create() { return TsDate::Create(); }
    void* ts_date_create_ms(int64_t ms) {
        // new Date(number): TimeClip the time value (|t| <= 8.64e15).
        if (ms == TsDate::INVALID) return TsDate::Create(TsDate::INVALID);
        return TsDate::Create(time_clip((double)ms));
    }
    void* ts_date_create_str(void* str) {
        // Single-arg path may receive a non-string (NaN-boxed number, null, etc.)
        // when inferredType fell through to string. Dispatch by NaN-box tag.
        uint64_t nb = (uint64_t)(uintptr_t)str;
        if (nanbox_is_double(nb) || nanbox_is_int32(nb)) {
            double d = nanbox_is_double(nb) ? nanbox_to_double(nb)
                                            : (double)nanbox_to_int32(nb);
            // TimeClip (ECMA-262 21.4.1.31): non-finite or |t| > 8.64e15 -> NaN.
            return TsDate::Create(time_clip(d));
        }
        if (nanbox_is_null(nb) || nanbox_is_undefined(nb) || !str) {
            return TsDate::Create(TsDate::INVALID);
        }
        if (nanbox_is_bool(nb)) {
            // ToNumber(true)=1, ToNumber(false)=0. Previously fell through to the
            // pointer path and crashed dereferencing the NaN-boxed boolean.
            return TsDate::Create((int64_t)(nanbox_to_bool(nb) ? 1 : 0));
        }
        // Heap pointer: extract the raw object and discriminate by the type magic
        // at offset 0 (both TsDate and TsString are PODs whose first field is the
        // magic). ECMA-262 21.4.2.2: when the single argument is an Object with a
        // [[DateValue]] internal slot (another Date), use thisTimeValue(value)
        // DIRECTLY, without invoking ToString/valueOf. Blindly casting a Date
        // object to TsString* and calling ToUtf8() read past its layout -> crash.
        void* p = nanbox_is_ptr(nb) ? nanbox_to_ptr(nb) : str;
        if (!p) return TsDate::Create(TsDate::INVALID);
        uint32_t magic0 = *(uint32_t*)p;
        if (magic0 == TsDate::MAGIC) {
            return TsDate::Create(((TsDate*)p)->GetTime());
        }
        if (magic0 == TsString::MAGIC) {
            return TsDate::Create(((TsString*)p)->ToUtf8());
        }
        // ECMA-262 21.4.2.2: the single-value Date(value) ctor does
        // ToPrimitive(value) then ToNumber on the result. ToNumber throws a
        // TypeError for a Symbol and for a BigInt, so `new Date(Symbol())` /
        // `new Date(0n)` must throw rather than coerce to a garbage timestamp.
        if (magic0 == 0x53594D42) {  // TsSymbol 'SYMB'
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a Symbol value to a number"));
            return TsDate::Create(TsDate::INVALID);  // unreachable
        }
        if (magic0 == 0x42494749) {  // TsBigInt 'BIGI'
            ts_throw((TsValue*)ts_error_create_typed("TypeError",
                "Cannot convert a BigInt value to a number"));
            return TsDate::Create(TsDate::INVALID);  // unreachable
        }
        // Any other object (e.g. new Date({})): ToPrimitive yields a string like
        // "[object Object]" which Date.parse rejects -> Invalid Date. Returning
        // Invalid here is spec-correct for the common case and, critically, no
        // longer crashes on an unexpected receiver layout.
        return TsDate::Create(TsDate::INVALID);
    }
    void* ts_date_create_parts(double y, double mo, double d,
                               double h, double mi, double s, double ms) {
        return TsDate::CreateFromParts(y, mo, d, h, mi, s, ms);
    }
    
    int64_t Date_getTime(void* date) { return ((TsDate*)date)->GetTime(); }
    int64_t Date_getFullYear(void* date) { return ((TsDate*)date)->GetFullYear(); }
    int64_t Date_getMonth(void* date) { return ((TsDate*)date)->GetMonth(); }
    int64_t Date_getDate(void* date) { return ((TsDate*)date)->GetDate(); }
    int64_t Date_getHours(void* date) { return ((TsDate*)date)->GetHours(); }
    int64_t Date_getMinutes(void* date) { return ((TsDate*)date)->GetMinutes(); }
    int64_t Date_getSeconds(void* date) { return ((TsDate*)date)->GetSeconds(); }
    int64_t Date_getMilliseconds(void* date) { return ((TsDate*)date)->GetMilliseconds(); }

    int64_t Date_getUTCFullYear(void* date) { return ((TsDate*)date)->GetUTCFullYear(); }
    int64_t Date_getUTCMonth(void* date) { return ((TsDate*)date)->GetUTCMonth(); }
    int64_t Date_getUTCDate(void* date) { return ((TsDate*)date)->GetUTCDate(); }
    int64_t Date_getUTCHours(void* date) { return ((TsDate*)date)->GetUTCHours(); }
    int64_t Date_getUTCMinutes(void* date) { return ((TsDate*)date)->GetUTCMinutes(); }
    int64_t Date_getUTCSeconds(void* date) { return ((TsDate*)date)->GetUTCSeconds(); }
    int64_t Date_getUTCMilliseconds(void* date) { return ((TsDate*)date)->GetUTCMilliseconds(); }

    void Date_setFullYear(void* date, int64_t year) { ((TsDate*)date)->SetFullYear(year); }
    void Date_setMonth(void* date, int64_t month) { ((TsDate*)date)->SetMonth(month); }
    void Date_setDate(void* date, int64_t day) { ((TsDate*)date)->SetDate(day); }
    void Date_setHours(void* date, int64_t hours) { ((TsDate*)date)->SetHours(hours); }
    void Date_setMinutes(void* date, int64_t minutes) { ((TsDate*)date)->SetMinutes(minutes); }
    void Date_setSeconds(void* date, int64_t seconds) { ((TsDate*)date)->SetSeconds(seconds); }
    void Date_setMilliseconds(void* date, int64_t ms) { ((TsDate*)date)->SetMilliseconds(ms); }

    void Date_setUTCFullYear(void* date, int64_t year) { ((TsDate*)date)->SetUTCFullYear(year); }
    void Date_setUTCMonth(void* date, int64_t month) { ((TsDate*)date)->SetUTCMonth(month); }
    void Date_setUTCDate(void* date, int64_t day) { ((TsDate*)date)->SetUTCDate(day); }
    void Date_setUTCHours(void* date, int64_t hours) { ((TsDate*)date)->SetUTCHours(hours); }
    void Date_setUTCMinutes(void* date, int64_t minutes) { ((TsDate*)date)->SetUTCMinutes(minutes); }
    void Date_setUTCSeconds(void* date, int64_t seconds) { ((TsDate*)date)->SetUTCSeconds(seconds); }
    void Date_setUTCMilliseconds(void* date, int64_t ms) { ((TsDate*)date)->SetUTCMilliseconds(ms); }

    void* Date_toISOString(void* date) { return ((TsDate*)date)->ToISOString(); }
    void* Date_toJSON(void* date) { return ((TsDate*)date)->ToJSON(); }
    void* Date_toString(void* date) { return ((TsDate*)date)->ToString(); }
    void* Date_toDateString(void* date) { return ((TsDate*)date)->ToDateString(); }

    int64_t Date_static_now() { return TsDate::Now(); }

    // ECMA-262 21.4.2.1: when Date is called as a function (not via new),
    // it ignores all args and returns the current time formatted as a
    // string (toString form).
    void* ts_date_now_string() {
        TsDate* d = TsDate::Create();
        return d->ToString();
    }
}
