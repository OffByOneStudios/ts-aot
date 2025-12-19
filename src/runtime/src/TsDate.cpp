#include "TsDate.h"
#include "TsString.h"
#include "GC.h"
#include <chrono>
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

TsDate* TsDate::Create(const char* dateStr) {
    UErrorCode status = U_ZERO_ERROR;
    // Use ISO 8601 as a primary format, but ICU can handle more
    icu::SimpleDateFormat fmt(icu::UnicodeString("yyyy-MM-dd'T'HH:mm:ss.SSSX"), status);
    UDate date = fmt.parse(icu::UnicodeString(dateStr), status);
    
    if (U_FAILURE(status)) {
        // Fallback to other formats or just current time
        return Create();
    }
    return Create((int64_t)date);
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
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> cal(icu::Calendar::createInstance(
        utc ? icu::TimeZone::createTimeZone("UTC") : icu::TimeZone::createDefault(), 
        status));
    cal->setTime((UDate)ms, status);
    int32_t val = cal->get(field, status);
    if (field == UCAL_MONTH) return val; // JS months are 0-11, ICU is also 0-11
    return val;
}

int64_t TsDate::GetFullYear() { return getField(ms, UCAL_YEAR, false); }
int64_t TsDate::GetMonth() { return getField(ms, UCAL_MONTH, false); }
int64_t TsDate::GetDate() { return getField(ms, UCAL_DATE, false); }
int64_t TsDate::GetHours() { return getField(ms, UCAL_HOUR_OF_DAY, false); }
int64_t TsDate::GetMinutes() { return getField(ms, UCAL_MINUTE, false); }
int64_t TsDate::GetSeconds() { return getField(ms, UCAL_SECOND, false); }
int64_t TsDate::GetMilliseconds() { return getField(ms, UCAL_MILLISECOND, false); }

int64_t TsDate::GetUTCFullYear() { return getField(ms, UCAL_YEAR, true); }
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
    cal->set(UCAL_YEAR, (int32_t)year);
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
    cal->set(UCAL_YEAR, (int32_t)year);
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

TsString* TsDate::ToString() {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::DateFormat> fmt(icu::DateFormat::createDateTimeInstance(icu::DateFormat::MEDIUM, icu::DateFormat::LONG, icu::Locale::getDefault()));
    icu::UnicodeString result;
    fmt->format((UDate)ms, result);
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
    void* ts_date_create_str(void* str) { return TsDate::Create(((TsString*)str)->ToUtf8()); }
    
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
    void* Date_toString(void* date) { return ((TsDate*)date)->ToString(); }
    void* Date_toDateString(void* date) { return ((TsDate*)date)->ToDateString(); }

    int64_t Date_static_now() { return TsDate::Now(); }
}
