#pragma once

class TsString {
public:
    // Factory method to create a new TsString on the GC heap
    static TsString* Create(const char* utf8Str);

    // Get UTF-8 representation (cached)
    const char* ToUtf8();

    static TsString* Concat(TsString* a, TsString* b);

private:
    TsString(const char* utf8Str);
    
    void* impl;       // Pointer to icu::UnicodeString
    char* utf8Buffer; // Cached UTF-8 buffer
};
