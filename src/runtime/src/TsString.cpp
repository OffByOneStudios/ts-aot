#include "TsString.h"
#include "TsArray.h"
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

TsString* TsString::Concat(TsString* a, TsString* b) {
    if (!a || !b) return nullptr; // Safety check
    icu::UnicodeString* s1 = static_cast<icu::UnicodeString*>(a->impl);
    icu::UnicodeString* s2 = static_cast<icu::UnicodeString*>(b->impl);
    
    icu::UnicodeString result = *s1 + *s2;
    
    std::string str;
    result.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::FromInt(int64_t value) {
    std::string str = std::to_string(value);
    return Create(str.c_str());
}

TsString* TsString::FromBool(bool value) {
    return Create(value ? "true" : "false");
}

TsString* TsString::FromDouble(double value) {
    std::string str = std::to_string(value);
    // Remove trailing zeros? std::to_string(double) produces 6 decimal places.
    // JS behavior is different.
    // For now, simple to_string is enough.
    return Create(str.c_str());
}

int64_t TsString::Length() {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    return s->length();
}

int64_t TsString::CharCodeAt(int64_t index) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    if (index < 0 || index >= s->length()) return 0;
    return s->charAt((int32_t)index);
}

void* TsString::Split(TsString* separator) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* sep = static_cast<icu::UnicodeString*>(separator->impl);
    
    TsArray* arr = TsArray::Create();
    
    if (sep->length() == 0) {
        // Split by character
        for (int32_t i = 0; i < s->length(); ++i) {
            icu::UnicodeString charStr = s->tempSubString(i, 1);
            std::string utf8;
            charStr.toUTF8String(utf8);
            arr->Push((int64_t)TsString::Create(utf8.c_str()));
        }
        return arr;
    }

    int32_t start = 0;
    int32_t pos = 0;
    
    while ((pos = s->indexOf(*sep, start)) != -1) {
        icu::UnicodeString sub = s->tempSubString(start, pos - start);
        std::string utf8;
        sub.toUTF8String(utf8);
        arr->Push((int64_t)TsString::Create(utf8.c_str()));
        start = pos + sep->length();
    }
    
    // Last part
    icu::UnicodeString sub = s->tempSubString(start);
    std::string utf8;
    sub.toUTF8String(utf8);
    arr->Push((int64_t)TsString::Create(utf8.c_str()));
    
    return arr;
}

TsString* TsString::Trim() {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString trimmed = *s;
    trimmed.trim();
    
    std::string utf8;
    trimmed.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::Substring(int64_t start, int64_t end) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    int32_t len = s->length();
    
    // Clamp to int32 range and string length
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    
    int32_t s32 = (int32_t)start;
    int32_t e32 = (int32_t)end;
    
    if (s32 > e32) std::swap(s32, e32);
    
    icu::UnicodeString sub = s->tempSubString(s32, e32 - s32);
    std::string utf8;
    sub.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

bool TsString::StartsWith(TsString* prefix) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* p = static_cast<icu::UnicodeString*>(prefix->impl);
    return s->startsWith(*p);
}

bool TsString::Includes(TsString* searchString) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* search = static_cast<icu::UnicodeString*>(searchString->impl);
    return s->indexOf(*search) != -1;
}

int64_t TsString::IndexOf(TsString* searchString) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* search = static_cast<icu::UnicodeString*>(searchString->impl);
    return s->indexOf(*search);
}

TsString* TsString::ToLowerCase() {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString result = *s;
    result.toLower();
    std::string str;
    result.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::ToUpperCase() {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString result = *s;
    result.toUpper();
    std::string str;
    result.toUTF8String(str);
    return Create(str.c_str());
}

bool TsString::Equals(TsString* other) {
    if (this == other) return true;
    if (!other) return false;
    icu::UnicodeString* s1 = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* s2 = static_cast<icu::UnicodeString*>(other->impl);
    return *s1 == *s2;
}

extern "C" {
    void* ts_string_create(const char* str) {
        return TsString::Create(str);
    }

    void* ts_string_concat(void* a, void* b) {
        return TsString::Concat((TsString*)a, (TsString*)b);
    }

    void* ts_string_split(void* str, void* separator) {
        return ((TsString*)str)->Split((TsString*)separator);
    }

    void* ts_string_trim(void* str) {
        return ((TsString*)str)->Trim();
    }

    void* ts_string_substring(void* str, int64_t start, int64_t end) {
        return ((TsString*)str)->Substring(start, end);
    }

    bool ts_string_startsWith(void* str, void* prefix) {
        return ((TsString*)str)->StartsWith((TsString*)prefix);
    }

    bool ts_string_includes(void* str, void* searchString) {
        return ((TsString*)str)->Includes((TsString*)searchString);
    }

    int64_t ts_string_indexOf(void* str, void* searchString) {
        return ((TsString*)str)->IndexOf((TsString*)searchString);
    }

    void* ts_string_toLowerCase(void* str) {
        return ((TsString*)str)->ToLowerCase();
    }

    void* ts_string_toUpperCase(void* str) {
        return ((TsString*)str)->ToUpperCase();
    }

    void* ts_string_from_int(int64_t value) {
        return TsString::FromInt(value);
    }

    void* ts_string_from_bool(bool value) {
        return TsString::FromBool(value);
    }

    void* ts_string_from_double(double value) {
        return TsString::FromDouble(value);
    }

    bool ts_string_eq(void* a, void* b) {
        return ((TsString*)a)->Equals((TsString*)b);
    }
}
