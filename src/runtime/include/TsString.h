#pragma once
#include <cstdint>

class TsString {
public:
    static constexpr uint32_t MAGIC = 0x53545247; // "STRG"

    // Factory method to create a new TsString on the GC heap
    static TsString* Create(const char* utf8Str);

    // Get UTF-8 representation (cached)
    const char* ToUtf8();
    const char* GetBuffer() { return ToUtf8(); }

    // Internal ICU access
    void* GetImpl() { return impl; }

    int64_t Length();
    int64_t CharCodeAt(int64_t index);
    void* Split(TsString* separator);
    TsString* Trim();
    TsString* Substring(int64_t start, int64_t end);
    bool StartsWith(TsString* prefix);
    bool Includes(TsString* searchString);
    int64_t IndexOf(TsString* searchString);
    TsString* ToLowerCase();
    TsString* ToUpperCase();

    static TsString* Concat(TsString* a, TsString* b);
    static TsString* FromInt(int64_t value);
    static TsString* FromBool(bool value);
    static TsString* FromDouble(double value);
    bool Equals(TsString* other);

private:
    TsString(const char* utf8Str);
    
    uint32_t magic = MAGIC;
    void* impl;       // Pointer to icu::UnicodeString
    char* utf8Buffer; // Cached UTF-8 buffer
};

extern "C" {
    int64_t ts_string_length(void* str);
    int64_t ts_string_charCodeAt(void* str, int64_t index);
    void* ts_string_split(void* str, void* separator);
    void* ts_string_trim(void* str);
    void* ts_string_substring(void* str, int64_t start, int64_t end);
    bool ts_string_startsWith(void* str, void* prefix);
    bool ts_string_includes(void* str, void* searchString);
    int64_t ts_string_indexOf(void* str, void* searchString);
    void* ts_string_toLowerCase(void* str);
    void* ts_string_toUpperCase(void* str);
    void* ts_string_from_int(int64_t value);
    void* ts_string_from_bool(bool value);
    void* ts_string_from_double(double value);
    bool ts_string_eq(void* a, void* b);
}
