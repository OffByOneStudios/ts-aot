#pragma once
#include <cstdint>
#include <unicode/regex.h>
#include "TsString.h"

class TsRegExp {
public:
    static constexpr uint32_t MAGIC = 0x52454758; // "REGX"
    static TsRegExp* Create(const char* pattern, const char* flags = "");
    
    bool Test(TsString* str);
    void* Exec(TsString* str); // Returns TsArray of matches or null

private:
    TsRegExp(const char* pattern, const char* flags);
    ~TsRegExp();

    uint32_t magic = MAGIC;
    icu::RegexMatcher* matcher = nullptr;
    icu::UnicodeString patternStr;
};

extern "C" {
    void* ts_regexp_create(void* pattern, void* flags);
    int32_t RegExp_test(void* re, void* str);
    void* RegExp_exec(void* re, void* str);
}
