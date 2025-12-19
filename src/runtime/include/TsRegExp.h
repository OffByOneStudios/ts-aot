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

    int64_t GetLastIndex() const { return lastIndex; }
    void SetLastIndex(int64_t index) { lastIndex = index; }

    TsString* GetSource() const;
    TsString* GetFlags() const;
    bool IsGlobal() const { return global; }
    bool IsSticky() const { return sticky; }
    bool IsIgnoreCase() const { return ignoreCase; }
    bool IsMultiline() const { return multiline; }
    void* GetMatcher() const { return matcher; }

private:
    TsRegExp(const char* pattern, const char* flags);
    ~TsRegExp();

    uint32_t magic = MAGIC;
    icu::RegexMatcher* matcher = nullptr;
    icu::UnicodeString patternStr;
    std::string flagsStr;
    int64_t lastIndex = 0;
    bool global = false;
    bool sticky = false;
    bool ignoreCase = false;
    bool multiline = false;
};

extern "C" {
    void* ts_regexp_create(void* pattern, void* flags);
    void* ts_regexp_from_literal(void* literal);
    int32_t RegExp_test(void* re, void* str);
    void* RegExp_exec(void* re, void* str);
    int64_t RegExp_get_lastIndex(void* re);
    void RegExp_set_lastIndex(void* re, int64_t index);
    void* RegExp_get_source(void* re);
    void* RegExp_get_flags(void* re);
    int32_t RegExp_get_global(void* re);
    int32_t RegExp_get_sticky(void* re);
    int32_t RegExp_get_ignoreCase(void* re);
    int32_t RegExp_get_multiline(void* re);
}
