#include "TsDate.h"
#include "TsString.h"
#include "TsNanBox.h"
#include "GC.h"
#include <chrono>
#include <cmath>
#include <new>
#include <unicode/calendar.h>
#include <unicode/gregocal.h>
#include <unicode/timezone.h>
#include <unicode/smpdtfmt.h>
#include <unicode/datefmt.h>

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
    // Per ECMA-262 §21.4.2.1: if any arg is NaN, produce an invalid Date.
    if (std::isnan(y) || std::isnan(mo) || std::isnan(d) ||
        std::isnan(h) || std::isnan(mi) || std::isnan(s) || std::isnan(ms)) {
        return Create(INVALID);
    }
    // Year 0-99 maps to 1900-1999 (legacy annexB semantics, spec §21.4.2.1 step 3d).
    int32_t year = (int32_t)y;
    if (year >= 0 && year <= 99) year += 1900;

    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->clear();
    if (year >= 1) {
        cal->set(UCAL_ERA, 1);
        cal->set(UCAL_YEAR, year);
    } else {
        cal->set(UCAL_ERA, 0);
        cal->set(UCAL_YEAR, 1 - year);
    }
    cal->set(UCAL_MONTH, (int32_t)mo);
    cal->set(UCAL_DATE, (int32_t)d);
    cal->set(UCAL_HOUR_OF_DAY, (int32_t)h);
    cal->set(UCAL_MINUTE, (int32_t)mi);
    cal->set(UCAL_SECOND, (int32_t)s);
    cal->set(UCAL_MILLISECOND, (int32_t)ms);
    int64_t t = (int64_t)cal->getTime(status);
    return Create(t);
}

TsDate* TsDate::Create(const char* dateStr) {
    UErrorCode status = U_ZERO_ERROR;
    // Use ISO 8601 as a primary format, but ICU can handle more
    icu::SimpleDateFormat fmt(icu::UnicodeString("yyyy-MM-dd'T'HH:mm:ss.SSSX"), status);
    UDate date = fmt.parse(icu::UnicodeString(dateStr), status);

    if (U_FAILURE(status)) {
        // Invalid date string → Invalid Date per spec.
        return Create(INVALID);
    }
    return Create((int64_t)date);
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

static int64_t getField(int64_t ms, UCalendarDateFields field, bool utc) {
    if (ms == TsDate::INVALID) return TsDate::INVALID;  // NaN propagation
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(
        utc ? icu::TimeZone::createTimeZone("UTC") : icu::TimeZone::createDefault(),
        status));
    cal->setTime((UDate)ms, status);
    int32_t val = cal->get(field, status);
    if (field == UCAL_MONTH) return val; // JS months are 0-11, ICU is also 0-11
    return val;
}

int64_t TsDate::GetFullYear() {
    if (ms == INVALID) return INVALID;
    // UCAL_EXTENDED_YEAR is signed; special-case to honor era (BC years go negative).
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    return cal->get(UCAL_EXTENDED_YEAR, status);
}
int64_t TsDate::GetMonth() { return getField(ms, UCAL_MONTH, false); }
int64_t TsDate::GetDate() { return getField(ms, UCAL_DATE, false); }
int64_t TsDate::GetHours() { return getField(ms, UCAL_HOUR_OF_DAY, false); }
int64_t TsDate::GetMinutes() { return getField(ms, UCAL_MINUTE, false); }
int64_t TsDate::GetSeconds() { return getField(ms, UCAL_SECOND, false); }
int64_t TsDate::GetMilliseconds() { return getField(ms, UCAL_MILLISECOND, false); }

int64_t TsDate::GetUTCFullYear() {
    if (ms == INVALID) return INVALID;
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(
        icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    return cal->get(UCAL_EXTENDED_YEAR, status);
}
int64_t TsDate::GetUTCMonth() { return getField(ms, UCAL_MONTH, true); }
int64_t TsDate::GetUTCDate() { return getField(ms, UCAL_DATE, true); }
int64_t TsDate::GetUTCHours() { return getField(ms, UCAL_HOUR_OF_DAY, true); }
int64_t TsDate::GetUTCMinutes() { return getField(ms, UCAL_MINUTE, true); }
int64_t TsDate::GetUTCSeconds() { return getField(ms, UCAL_SECOND, true); }
int64_t TsDate::GetUTCMilliseconds() { return getField(ms, UCAL_MILLISECOND, true); }

void TsDate::SetFullYear(int64_t year) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    // ICU GregorianCalendar uses ERA+YEAR for writes (kDatePrecedence puts YEAR first).
    // Translate signed JS year: y>=1 → AD year=y; y<=0 → BC year=(1-y).
    if (year >= 1) {
        cal->set(UCAL_ERA, 1);
        cal->set(UCAL_YEAR, (int32_t)year);
    } else {
        cal->set(UCAL_ERA, 0);
        cal->set(UCAL_YEAR, (int32_t)(1 - year));
    }
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetMonth(int64_t month) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_MONTH, (int32_t)month);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetDate(int64_t date) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_DATE, (int32_t)date);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetHours(int64_t hours) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_HOUR_OF_DAY, (int32_t)hours);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetMinutes(int64_t minutes) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_MINUTE, (int32_t)minutes);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetSeconds(int64_t seconds) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_SECOND, (int32_t)seconds);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetMilliseconds(int64_t milliseconds) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_MILLISECOND, (int32_t)milliseconds);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCFullYear(int64_t year) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    if (year >= 1) {
        cal->set(UCAL_ERA, 1);
        cal->set(UCAL_YEAR, (int32_t)year);
    } else {
        cal->set(UCAL_ERA, 0);
        cal->set(UCAL_YEAR, (int32_t)(1 - year));
    }
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCMonth(int64_t month) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_MONTH, (int32_t)month);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCDate(int64_t date) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_DATE, (int32_t)date);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCHours(int64_t hours) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_HOUR_OF_DAY, (int32_t)hours);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCMinutes(int64_t minutes) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_MINUTE, (int32_t)minutes);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCSeconds(int64_t seconds) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_SECOND, (int32_t)seconds);
    ms = (int64_t)cal->getTime(status);
}

void TsDate::SetUTCMilliseconds(int64_t milliseconds) {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(icu::TimeZone::createTimeZone("UTC"), status));
    cal->setTime((UDate)ms, status);
    cal->set(UCAL_MILLISECOND, (int32_t)milliseconds);
    ms = (int64_t)cal->getTime(status);
}

TsString* TsDate::ToISOString() {
    UErrorCode status = U_ZERO_ERROR;
    icu::SimpleDateFormat fmt(icu::UnicodeString("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'"), status);
    fmt.setTimeZone(*icu::TimeZone::getGMT());
    icu::UnicodeString result;
    fmt.format((UDate)ms, result);
    std::string utf8;
    result.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsDate::ToJSON() {
    // toJSON returns the same as toISOString for Date objects
    return ToISOString();
}

TsString* TsDate::ToString() {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::DateFormat> fmt(icu::DateFormat::createDateTimeInstance(icu::DateFormat::MEDIUM, icu::DateFormat::LONG, icu::Locale::getDefault()));
    icu::UnicodeString result;
    fmt->format((UDate)ms, result);
    std::string utf8;
    result.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsDate::ToUTCString() {
    UErrorCode status = U_ZERO_ERROR;
    icu::SimpleDateFormat fmt(icu::UnicodeString("EEE, dd MMM yyyy HH:mm:ss 'GMT'"), icu::Locale("en_US"), status);
    fmt.setTimeZone(*icu::TimeZone::getGMT());
    icu::UnicodeString result;
    fmt.format((UDate)ms, result);
    std::string utf8;
    result.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsDate::ToDateString() {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::DateFormat> fmt(icu::DateFormat::createDateInstance(icu::DateFormat::MEDIUM, icu::Locale::getDefault()));
    icu::UnicodeString result;
    fmt->format((UDate)ms, result);
    std::string utf8;
    result.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

int64_t TsDate::Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

extern "C" {
    void* ts_date_create() { return TsDate::Create(); }
    void* ts_date_create_ms(int64_t ms) { return TsDate::Create(ms); }
    void* ts_date_create_str(void* str) {
        // Single-arg path may receive a non-string (NaN-boxed number, null, etc.)
        // when inferredType fell through to string. Dispatch by NaN-box tag.
        uint64_t nb = (uint64_t)(uintptr_t)str;
        if (nanbox_is_double(nb) || nanbox_is_int32(nb)) {
            double d = nanbox_is_double(nb) ? nanbox_to_double(nb)
                                            : (double)nanbox_to_int32(nb);
            if (std::isnan(d)) return TsDate::Create(TsDate::INVALID);
            return TsDate::Create((int64_t)d);
        }
        if (nanbox_is_null(nb) || nanbox_is_undefined(nb) || !str) {
            return TsDate::Create(TsDate::INVALID);
        }
        // Treat as string and parse.
        return TsDate::Create(((TsString*)str)->ToUtf8());
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
