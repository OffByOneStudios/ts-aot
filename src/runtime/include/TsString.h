#pragma once
#include <cstdint>

class TsString {
public:
    // Factory method to create a new TsString on the GC heap
    static TsString* Create(const char* utf8Str);

    // Get UTF-8 representation (cached)
    const char* ToUtf8();

    int64_t Length();
    int64_t CharCodeAt(int64_t index);
    void* Split(TsString* separator);

    static TsString* Concat(TsString* a, TsString* b);

private:
    TsString(const char* utf8Str);
    
    void* impl;       // Pointer to icu::UnicodeString
    char* utf8Buffer; // Cached UTF-8 buffer
};

extern "C" {
    int64_t ts_string_length(void* str);
    int64_t ts_string_charCodeAt(void* str, int64_t index);
    void* ts_string_split(void* str, void* separator);
}
