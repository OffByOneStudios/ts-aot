#pragma once
#include <cstdint>

class TsDate {
public:
    static constexpr uint32_t MAGIC = 0x44415445; // "DATE"
    static TsDate* Create();
    static TsDate* Create(int64_t milliseconds);
    static TsDate* Create(const char* dateStr);

    int64_t GetTime();
    int64_t GetFullYear();
    int64_t GetMonth();
    int64_t GetDate();
    int64_t GetHours();
    int64_t GetMinutes();
    int64_t GetSeconds();
    int64_t GetMilliseconds();

    int64_t GetUTCFullYear();
    int64_t GetUTCMonth();
    int64_t GetUTCDate();
    int64_t GetUTCHours();
    int64_t GetUTCMinutes();
    int64_t GetUTCSeconds();
    int64_t GetUTCMilliseconds();

    void SetFullYear(int64_t year);
    void SetMonth(int64_t month);
    void SetDate(int64_t date);
    void SetHours(int64_t hours);
    void SetMinutes(int64_t minutes);
    void SetSeconds(int64_t seconds);
    void SetMilliseconds(int64_t ms);

    void SetUTCFullYear(int64_t year);
    void SetUTCMonth(int64_t month);
    void SetUTCDate(int64_t date);
    void SetUTCHours(int64_t hours);
    void SetUTCMinutes(int64_t minutes);
    void SetUTCSeconds(int64_t seconds);
    void SetUTCMilliseconds(int64_t ms);

    class TsString* ToISOString();
    class TsString* ToJSON();
    class TsString* ToString();
    class TsString* ToDateString();

    static int64_t Now();

private:
    TsDate();
    TsDate(int64_t milliseconds);
    
    uint32_t magic = MAGIC;
    int64_t ms; // Milliseconds since epoch
};

extern "C" {
    void* ts_date_create();
    void* ts_date_create_ms(int64_t ms);
    void* ts_date_create_str(void* str);
    int64_t Date_getTime(void* date);
    int64_t Date_getFullYear(void* date);
    int64_t Date_getMonth(void* date);
    int64_t Date_getDate(void* date);
    int64_t Date_getHours(void* date);
    int64_t Date_getMinutes(void* date);
    int64_t Date_getSeconds(void* date);
    int64_t Date_getMilliseconds(void* date);

    int64_t Date_getUTCFullYear(void* date);
    int64_t Date_getUTCMonth(void* date);
    int64_t Date_getUTCDate(void* date);
    int64_t Date_getUTCHours(void* date);
    int64_t Date_getUTCMinutes(void* date);
    int64_t Date_getUTCSeconds(void* date);
    int64_t Date_getUTCMilliseconds(void* date);

    void Date_setFullYear(void* date, int64_t year);
    void Date_setMonth(void* date, int64_t month);
    void Date_setDate(void* date, int64_t day);
    void Date_setHours(void* date, int64_t hours);
    void Date_setMinutes(void* date, int64_t minutes);
    void Date_setSeconds(void* date, int64_t seconds);
    void Date_setMilliseconds(void* date, int64_t ms);

    void Date_setUTCFullYear(void* date, int64_t year);
    void Date_setUTCMonth(void* date, int64_t month);
    void Date_setUTCDate(void* date, int64_t day);
    void Date_setUTCHours(void* date, int64_t hours);
    void Date_setUTCMinutes(void* date, int64_t minutes);
    void Date_setUTCSeconds(void* date, int64_t seconds);
    void Date_setUTCMilliseconds(void* date, int64_t ms);

    void* Date_toISOString(void* date);
    void* Date_toJSON(void* date);
    void* Date_toString(void* date);
    void* Date_toDateString(void* date);

    int64_t Date_static_now();
}
