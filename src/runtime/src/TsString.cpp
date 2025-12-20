#include "TsString.h"
#include "TsArray.h"
#include "TsRegExp.h"
#include "TsRuntime.h"
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
            arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
        }
        return arr;
    }

    int32_t start = 0;
    int32_t pos = 0;
    
    while ((pos = s->indexOf(*sep, start)) != -1) {
        icu::UnicodeString sub = s->tempSubString(start, pos - start);
        std::string utf8;
        sub.toUTF8String(utf8);
        arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
        start = pos + sep->length();
    }
    
    // Last part
    icu::UnicodeString sub = s->tempSubString(start);
    std::string utf8;
    sub.toUTF8String(utf8);
    arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
    
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

TsString* TsString::Slice(int64_t start, int64_t end) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    int32_t len = s->length();
    
    if (start < 0) start = len + start;
    if (start < 0) start = 0;
    if (start > len) start = len;
    
    if (end < 0) end = len + end;
    if (end > len) end = len;
    if (end < start) end = start;
    
    icu::UnicodeString sub = s->tempSubString((int32_t)start, (int32_t)(end - start));
    std::string utf8;
    sub.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::Repeat(int64_t count) {
    if (count <= 0) return Create("");
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString result;
    for (int64_t i = 0; i < count; ++i) {
        result += *s;
    }
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::PadStart(int64_t targetLength, TsString* padString) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    int32_t len = s->length();
    if (targetLength <= len) return this;
    
    icu::UnicodeString* pad = static_cast<icu::UnicodeString*>(padString->impl);
    if (pad->length() == 0) return this;
    
    icu::UnicodeString result;
    int32_t padLen = (int32_t)targetLength - len;
    while (result.length() < padLen) {
        result += *pad;
    }
    if (result.length() > padLen) {
        result.truncate(padLen);
    }
    result += *s;
    
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::PadEnd(int64_t targetLength, TsString* padString) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    int32_t len = s->length();
    if (targetLength <= len) return this;
    
    icu::UnicodeString* pad = static_cast<icu::UnicodeString*>(padString->impl);
    if (pad->length() == 0) return this;
    
    icu::UnicodeString result = *s;
    int32_t padLen = (int32_t)targetLength - len;
    icu::UnicodeString padding;
    while (padding.length() < padLen) {
        padding += *pad;
    }
    if (padding.length() > padLen) {
        padding.truncate(padLen);
    }
    result += padding;
    
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
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

TsString* TsString::Replace(TsString* pattern, TsString* replacement) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* p = static_cast<icu::UnicodeString*>(pattern->impl);
    icu::UnicodeString* r = static_cast<icu::UnicodeString*>(replacement->impl);
    
    icu::UnicodeString result = *s;
    int32_t pos = result.indexOf(*p);
    if (pos != -1) {
        result.replace(pos, p->length(), *r);
    }
    
    std::string str;
    result.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::ReplaceAll(TsString* pattern, TsString* replacement) {
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* p = static_cast<icu::UnicodeString*>(pattern->impl);
    icu::UnicodeString* r = static_cast<icu::UnicodeString*>(replacement->impl);
    
    icu::UnicodeString result = *s;
    result.findAndReplace(*p, *r);
    
    std::string str;
    result.toUTF8String(str);
    return Create(str.c_str());
}

void* TsString::Match(TsRegExp* regexp) {
    if (!regexp) return nullptr;
    
    // If not global, same as exec
    if (regexp->GetFlags()->Includes(TsString::Create("g")) == false) {
        return regexp->Exec(this);
    }
    
    // Global match: return all full matches
    TsArray* results = TsArray::Create();
    regexp->SetLastIndex(0);
    
    while (true) {
        void* execResult = regexp->Exec(this);
        if (!execResult) break;
        
        TsArray* arr = (TsArray*)execResult;
        // In global match, we only want the full match (index 0)
        results->Push(arr->At(0));
    }
    
    if (results->Length() == 0) return nullptr;
    return results;
}

int64_t TsString::Search(TsRegExp* regexp) {
    if (!regexp) return -1;
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    matcher->reset(*s);
    if (matcher->find()) {
        UErrorCode status = U_ZERO_ERROR;
        return matcher->start(status);
    }
    return -1;
}

TsString* TsString::Replace(TsRegExp* regexp, TsString* replacement) {
    if (!regexp || !replacement) return this;
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    icu::UnicodeString* r = static_cast<icu::UnicodeString*>(replacement->impl);
    
    UErrorCode status = U_ZERO_ERROR;
    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    matcher->reset(*s);
    
    icu::UnicodeString result;
    if (regexp->GetFlags()->Includes(TsString::Create("g"))) {
        result = matcher->replaceAll(*r, status);
    } else {
        result = matcher->replaceFirst(*r, status);
    }
    
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

void* TsString::Split(TsRegExp* regexp) {
    if (!regexp) return Split(TsString::Create(""));
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(impl);
    UErrorCode status = U_ZERO_ERROR;
    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    matcher->reset(*s);
    
    TsArray* arr = TsArray::Create();
    int32_t start = 0;
    
    while (matcher->find()) {
        int32_t matchStart = matcher->start(status);
        int32_t matchEnd = matcher->end(status);
        
        icu::UnicodeString sub = s->tempSubString(start, matchStart - start);
        std::string utf8;
        sub.toUTF8String(utf8);
        arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
        
        // If there are capturing groups, they are included in the result
        int32_t groupCount = matcher->groupCount();
        for (int32_t i = 1; i <= groupCount; ++i) {
            icu::UnicodeString group = matcher->group(i, status);
            if (matcher->start(i, status) != -1) {
                std::string gUtf8;
                group.toUTF8String(gUtf8);
                arr->Push((int64_t)ts_value_make_string(TsString::Create(gUtf8.c_str())));
            } else {
                arr->Push((int64_t)ts_value_make_undefined());
            }
        }
        
        start = matchEnd;
    }
    
    // Last part
    icu::UnicodeString sub = s->tempSubString(start);
    std::string utf8;
    sub.toUTF8String(utf8);
    arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
    
    return arr;
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

    int64_t ts_string_length(void* str) {
        return ((TsString*)str)->Length();
    }

    int64_t ts_string_charCodeAt(void* str, int64_t index) {
        return ((TsString*)str)->CharCodeAt(index);
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

    void* ts_string_slice(void* str, int64_t start, int64_t end) {
        return ((TsString*)str)->Slice(start, end);
    }

    void* ts_string_repeat(void* str, int64_t count) {
        return ((TsString*)str)->Repeat(count);
    }

    void* ts_string_padStart(void* str, int64_t targetLength, void* padString) {
        return ((TsString*)str)->PadStart(targetLength, (TsString*)padString);
    }

    void* ts_string_padEnd(void* str, int64_t targetLength, void* padString) {
        return ((TsString*)str)->PadEnd(targetLength, (TsString*)padString);
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

    void* ts_string_match_regexp(void* str, void* regexp) {
        return ((TsString*)str)->Match((TsRegExp*)regexp);
    }

    int64_t ts_string_search_regexp(void* str, void* regexp) {
        return ((TsString*)str)->Search((TsRegExp*)regexp);
    }

    void* ts_string_replace(void* str, void* pattern, void* replacement) {
        return ((TsString*)str)->Replace((TsString*)pattern, (TsString*)replacement);
    }

    void* ts_string_replace_regexp(void* str, void* regexp, void* replacement) {
        return ((TsString*)str)->Replace((TsRegExp*)regexp, (TsString*)replacement);
    }

    void* ts_string_replaceAll(void* str, void* pattern, void* replacement) {
        return ((TsString*)str)->ReplaceAll((TsString*)pattern, (TsString*)replacement);
    }

    void* ts_string_split_regexp(void* str, void* regexp) {
        return ((TsString*)str)->Split((TsRegExp*)regexp);
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

    void* ts_string_from_value(TsValue* val) {
        if (!val) return TsString::Create("undefined");
        switch (val->type) {
            case ValueType::UNDEFINED: return TsString::Create("undefined");
            case ValueType::NUMBER_INT: return TsString::FromInt(val->i_val);
            case ValueType::NUMBER_DBL: return TsString::FromDouble(val->d_val);
            case ValueType::BOOLEAN: return TsString::FromBool(val->b_val);
            case ValueType::STRING_PTR: return val->ptr_val;
            case ValueType::OBJECT_PTR: return TsString::Create("[object Object]");
            case ValueType::ARRAY_PTR: return TsString::Create("[object Array]");
            case ValueType::PROMISE_PTR: return TsString::Create("[object Promise]");
            default: return TsString::Create("unknown");
        }
    }
}
