#pragma once
#include <cstdint>
#include <unicode/unistr.h>

class TsString {
public:
    static constexpr uint32_t MAGIC = 0x53545247; // "STRG"

    // Factory method to create a new TsString on the GC heap
    static TsString* Create(const char* utf8Str);
    
    // Factory method with string interning (cache shared strings)
    static TsString* GetInterned(const char* utf8Str);

    // Get UTF-8 representation (cached)
    const char* ToUtf8();
    const char* GetBuffer() { return ToUtf8(); }

    // Internal ICU access
    icu::UnicodeString ToUnicodeString() const;

    int64_t Length();
    int64_t CharCodeAt(int64_t index);
    TsString* CharAt(int64_t index);
    void* Split(TsString* separator);
    TsString* Trim();
    TsString* Substring(int64_t start, int64_t end);
    TsString* Slice(int64_t start, int64_t end);
    TsString* Repeat(int64_t count);
    TsString* PadStart(int64_t targetLength, TsString* padString);
    TsString* PadEnd(int64_t targetLength, TsString* padString);
    bool StartsWith(TsString* prefix);
    bool Includes(TsString* searchString);
    int64_t IndexOf(TsString* searchString);
    int64_t LastIndexOf(TsString* searchString);
    TsString* ToLowerCase();
    TsString* ToUpperCase();
    
    void* Match(class TsRegExp* regexp);
    int64_t Search(class TsRegExp* regexp);
    TsString* Replace(TsString* pattern, TsString* replacement);
    TsString* Replace(class TsRegExp* regexp, TsString* replacement);
    TsString* ReplaceAll(TsString* pattern, TsString* replacement);
    
    void* Split(class TsRegExp* regexp);

    static TsString* Concat(TsString* a, TsString* b);
    static TsString* FromInt(int64_t value);
    static TsString* FromBool(bool value);
    static TsString* FromDouble(double value);
    bool Equals(TsString* other);
    
    uint32_t magic = MAGIC;

private:
    TsString(const char* utf8Str);
    TsString(const char* utf8Str, uint32_t len);
    uint32_t length = 0;
    bool isSmall = false;
    union {
        struct {
            void* impl;       // Pointer to icu::UnicodeString
            char* utf8Buffer; // Cached UTF-8 buffer
        } heap;
        char inlineBuffer[64];  // Increased from 16 to handle most property names
    } data;
};

extern "C" {
    int64_t ts_string_length(void* str);
    int64_t ts_string_charCodeAt(void* str, int64_t index);
    void* ts_string_charAt(void* str, int64_t index);
    void* ts_string_split(void* str, void* separator);
    void* ts_string_split_regexp(void* str, void* regexp);
    void* ts_string_trim(void* str);
    void* ts_string_substring(void* str, int64_t start, int64_t end);
    void* ts_string_slice(void* str, int64_t start, int64_t end);
    void* ts_string_repeat(void* str, int64_t count);
    void* ts_string_padStart(void* str, int64_t targetLength, void* padString);
    void* ts_string_padEnd(void* str, int64_t targetLength, void* padString);
    bool ts_string_startsWith(void* str, void* prefix);
    bool ts_string_includes(void* str, void* searchString);
    int64_t ts_string_indexOf(void* str, void* searchString);
    int64_t ts_string_lastIndexOf(void* str, void* searchString);
    void* ts_string_toLowerCase(void* str);
    void* ts_string_toUpperCase(void* str);
    void* ts_string_match_regexp(void* str, void* regexp);
    void* ts_string_replace(void* str, void* pattern, void* replacement);
    void* ts_string_replace_regexp(void* str, void* regexp, void* replacement);
    void* ts_string_replaceAll(void* str, void* pattern, void* replacement);
    void* ts_string_from_int(int64_t value);
    void* ts_string_from_bool(bool value);
    void* ts_string_from_double(double value);
    bool ts_string_eq(void* a, void* b);
}
