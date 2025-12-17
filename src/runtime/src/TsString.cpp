#include "TsString.h"
#include "GC.h"
#include <unicode/unistr.h>
#include <new>
#include <string>
#include <cstring>

TsString* TsString::Create(const char* utf8Str) {
    void* mem = ts_alloc(sizeof(TsString));
    return new(mem) TsString(utf8Str);
}

TsString::TsString(const char* utf8Str) {
    // Allocate the ICU string on the GC heap as well
    void* mem = ts_alloc(sizeof(icu::UnicodeString));
    impl = new(mem) icu::UnicodeString(utf8Str);
    utf8Buffer = nullptr;
    
    // TODO: Register finalizer to clean up icu::UnicodeString's internal allocation
    // if it allocates outside of our GC.
}

const char* TsString::ToUtf8() {
    if (utf8Buffer) return utf8Buffer;
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    std::string str;
    s->toUTF8String(str);
    
    size_t len = str.length();
    utf8Buffer = (char*)ts_alloc(len + 1);
    std::memcpy(utf8Buffer, str.c_str(), len + 1);
    return utf8Buffer;
}

extern "C" TsString* ts_string_create(const char* str) {
    return TsString::Create(str);
}
