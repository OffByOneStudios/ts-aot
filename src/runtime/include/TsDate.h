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
    int64_t Date_static_now();
}
