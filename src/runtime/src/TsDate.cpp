#include "TsDate.h"
#include "TsString.h"
#include "GC.h"
#include <chrono>
#include <ctime>
#include <new>

TsDate* TsDate::Create() {
    void* mem = ts_alloc(sizeof(TsDate));
    return new(mem) TsDate();
}

TsDate* TsDate::Create(int64_t milliseconds) {
    void* mem = ts_alloc(sizeof(TsDate));
    return new(mem) TsDate(milliseconds);
}

TsDate* TsDate::Create(const char* dateStr) {
    // Simple parsing for now, could use ICU for better parsing
    // For now, just return current date if parsing fails
    return Create();
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

int64_t TsDate::GetFullYear() {
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    return tm->tm_year + 1900;
}

int64_t TsDate::GetMonth() {
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    return tm->tm_mon;
}

int64_t TsDate::GetDate() {
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    return tm->tm_mday;
}

int64_t TsDate::GetHours() {
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    return tm->tm_hour;
}

int64_t TsDate::GetMinutes() {
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    return tm->tm_min;
}

int64_t TsDate::GetSeconds() {
    std::time_t t = ms / 1000;
    std::tm* tm = std::gmtime(&t);
    return tm->tm_sec;
}

int64_t TsDate::Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

extern "C" {
    void* ts_date_create() {
        return TsDate::Create();
    }
    void* ts_date_create_ms(int64_t ms) {
        return TsDate::Create(ms);
    }
    void* ts_date_create_str(void* str) {
        return TsDate::Create(((TsString*)str)->ToUtf8());
    }
    int64_t Date_getTime(void* date) {
        return ((TsDate*)date)->GetTime();
    }
    int64_t Date_getFullYear(void* date) {
        return ((TsDate*)date)->GetFullYear();
    }
    int64_t Date_getMonth(void* date) {
        return ((TsDate*)date)->GetMonth();
    }
    int64_t Date_getDate(void* date) {
        return ((TsDate*)date)->GetDate();
    }
    int64_t Date_static_now() {
        return TsDate::Now();
    }
}
