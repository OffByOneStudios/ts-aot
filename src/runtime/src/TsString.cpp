#include "TsString.h"
#include "TsArray.h"
#include "TsRegExp.h"
#include "TsRuntime.h"
#include "GC.h"
#include <cmath>
#include <unicode/unistr.h>
#include <new>
#include <string>
#include <cstring>
#include <unordered_map>

// CRITICAL FIX: Cache for common numeric strings (0-999)
// Lodash does thousands of numeric property accesses which were allocating new strings each time
static std::unordered_map<int64_t, TsString*> numericStringCache;
static const int MAX_CACHED_INT = 9999;  // Expanded from 999
static const int MIN_CACHED_INT = -100;  // Cache negative numbers too

// OPTIMIZATION: Global string interning pool
// Caches ALL strings to eliminate duplicate allocations during property access
static std::unordered_map<std::string, TsString*> globalStringCache;
static const size_t MAX_INTERN_SIZE = 256; // Only intern strings <= 256 chars
static const size_t MAX_CACHE_SIZE = 10000; // Prevent unbounded growth

TsString* TsString::Create(const char* utf8Str) {
    if (!utf8Str) utf8Str = "";
    size_t len = std::strlen(utf8Str);
    
    // Use inline buffer for ASCII strings < 64 bytes (covers most property names)
    bool isAscii = true;
    if (len < 64) {
        for (size_t i = 0; i < len; ++i) {
            if ((unsigned char)utf8Str[i] > 127) {
                isAscii = false;
                break;
            }
        }
    } else {
        isAscii = false;
    }

    void* mem = ts_alloc(sizeof(TsString));
    if (isAscii) {
        return new(mem) TsString(utf8Str, (uint32_t)len);
    } else {
        return new(mem) TsString(utf8Str);
    }
}

TsString* TsString::GetInterned(const char* utf8Str) {
    if (!utf8Str) return Create(""); // Empty string fallback
    
    size_t len = std::strlen(utf8Str);
    
    // Don't intern very large strings
    if (len > MAX_INTERN_SIZE) {
        return Create(utf8Str);
    }
    
    // Check cache
    std::string key(utf8Str, len);
    auto it = globalStringCache.find(key);
    if (it != globalStringCache.end()) {
        return it->second; // Cache hit!
    }
    
    // Cache miss - create new string
    TsString* str = Create(utf8Str);
    
    // Add to cache if not too large
    if (globalStringCache.size() < MAX_CACHE_SIZE) {
        globalStringCache[key] = str;
    }
    
    return str;
}

TsString::TsString(const char* utf8Str, uint32_t len) {
    magic = MAGIC;
    isSmall = true;
    length = len;
    std::memcpy(data.inlineBuffer, utf8Str, len);
    if (len < 64) data.inlineBuffer[len] = '\0';
}

TsString::TsString(const char* utf8Str) {
    magic = MAGIC;
    isSmall = false;
    // Allocate the ICU string on the GC heap as well
    void* mem = ts_alloc(sizeof(icu::UnicodeString));
    data.heap.impl = new(mem) icu::UnicodeString(utf8Str);
    data.heap.utf8Buffer = nullptr;
    length = static_cast<icu::UnicodeString*>(data.heap.impl)->length();
}

const char* TsString::ToUtf8() {
    if (isSmall) return data.inlineBuffer;
    if (data.heap.utf8Buffer) return data.heap.utf8Buffer;
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    std::string str;
    s->toUTF8String(str);
    
    size_t len = str.length();
    data.heap.utf8Buffer = (char*)ts_alloc(len + 1);
    std::memcpy(data.heap.utf8Buffer, str.c_str(), len + 1);
    return data.heap.utf8Buffer;
}

TsString* TsString::Concat(TsString* a, TsString* b) {
    if (!a || !b) return nullptr; // Safety check
    
    if (a->isSmall && b->isSmall && (a->length + b->length < 16)) {
        char buf[16];
        std::memcpy(buf, a->data.inlineBuffer, a->length);
        std::memcpy(buf + a->length, b->data.inlineBuffer, b->length);
        buf[a->length + b->length] = '\0';
        return Create(buf);
    }

    icu::UnicodeString s1;
    if (a->isSmall) {
        s1 = icu::UnicodeString::fromUTF8(a->data.inlineBuffer);
    } else {
        s1 = *static_cast<icu::UnicodeString*>(a->data.heap.impl);
    }

    icu::UnicodeString s2;
    if (b->isSmall) {
        s2 = icu::UnicodeString::fromUTF8(b->data.inlineBuffer);
    } else {
        s2 = *static_cast<icu::UnicodeString*>(b->data.heap.impl);
    }
    
    icu::UnicodeString result = s1 + s2;
    
    std::string str;
    result.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::FromInt(int64_t value) {
    // Check cache for common values (0-999)
    if (value >= MIN_CACHED_INT && value <= MAX_CACHED_INT) {
        auto it = numericStringCache.find(value);
        if (it != numericStringCache.end()) {
            return it->second;
        }
        
        // Not in cache, create and cache it
        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), "%lld", value);
        TsString* str = Create(buf);
        numericStringCache[value] = str;
        return str;
    }
    
    // Outside cache range, create normally
    char buf[32];
    int len = std::snprintf(buf, sizeof(buf), "%lld", value);
    return Create(buf);
}

TsString* TsString::FromBool(bool value) {
    return GetInterned(value ? "true" : "false");
}

TsString* TsString::FromDouble(double value) {
    if (std::isnan(value)) return GetInterned("NaN");
    if (std::isinf(value)) return GetInterned(value < 0 ? "-Infinity" : "Infinity");
    
    // If it's an integer, use FromInt (which has better caching)
    if (std::floor(value) == value && value >= MIN_CACHED_INT && value <= MAX_CACHED_INT) {
        return FromInt((int64_t)value);
    }

    // Cache common non-integer doubles
    static std::unordered_map<double, TsString*> doubleCache;
    static const size_t MAX_DOUBLE_CACHE = 1000;
    
    auto it = doubleCache.find(value);
    if (it != doubleCache.end()) {
        return it->second;
    }

    char buf[64];
    int len = std::snprintf(buf, sizeof(buf), "%g", value);
    TsString* str = Create(buf);
    
    // Cache if not too large
    if (doubleCache.size() < MAX_DOUBLE_CACHE) {
        doubleCache[value] = str;
    }
    
    return str;
}

int64_t TsString::Length() {
    return length;
}

int64_t TsString::CharCodeAt(int64_t index) {
    if (index < 0 || index >= length) return 0;
    if (isSmall) return (unsigned char)data.inlineBuffer[index];
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    return s->charAt((int32_t)index);
}

TsString* TsString::CharAt(int64_t index) {
    if (index < 0 || index >= length) return TsString::Create("");
    if (isSmall) {
        char buf[2] = { data.inlineBuffer[index], 0 };
        return TsString::Create(buf);
    }
    
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    icu::UnicodeString charStr = s->tempSubString((int32_t)index, 1);
    std::string utf8;
    charStr.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

icu::UnicodeString TsString::ToUnicodeString() const {
    if (isSmall) {
        return icu::UnicodeString::fromUTF8(data.inlineBuffer);
    }
    return *static_cast<icu::UnicodeString*>(data.heap.impl);
}

void* TsString::Split(TsString* separator) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString sep;
    if (separator->isSmall) sep = icu::UnicodeString::fromUTF8(separator->data.inlineBuffer);
    else sep = *static_cast<icu::UnicodeString*>(separator->data.heap.impl);
    
    TsArray* arr = TsArray::Create();
    
    if (sep.length() == 0) {
        // Split by character
        for (int32_t i = 0; i < s.length(); ++i) {
            icu::UnicodeString charStr = s.tempSubString(i, 1);
            std::string utf8;
            charStr.toUTF8String(utf8);
            arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
        }
        return arr;
    }

    int32_t start = 0;
    int32_t pos = 0;
    
    while ((pos = s.indexOf(sep, start)) != -1) {
        icu::UnicodeString sub = s.tempSubString(start, pos - start);
        std::string utf8;
        sub.toUTF8String(utf8);
        arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
        start = pos + sep.length();
    }
    
    // Last part
    icu::UnicodeString sub = s.tempSubString(start);
    std::string utf8;
    sub.toUTF8String(utf8);
    arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
    
    return arr;
}

TsString* TsString::Trim() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    s.trim();
    
    std::string utf8;
    s.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::Substring(int64_t start, int64_t end) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    int32_t len = s.length();
    
    // Clamp to int32 range and string length
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    
    int32_t s32 = (int32_t)start;
    int32_t e32 = (int32_t)end;
    
    if (s32 > e32) std::swap(s32, e32);
    
    icu::UnicodeString sub = s.tempSubString(s32, e32 - s32);
    std::string utf8;
    sub.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::Slice(int64_t start, int64_t end) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    int32_t len = s.length();
    
    if (start < 0) start = len + start;
    if (start < 0) start = 0;
    if (start > len) start = len;
    
    if (end < 0) end = len + end;
    if (end > len) end = len;
    if (end < start) end = start;
    
    icu::UnicodeString sub = s.tempSubString((int32_t)start, (int32_t)(end - start));
    std::string utf8;
    sub.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::Repeat(int64_t count) {
    if (count <= 0) return Create("");
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString result;
    for (int64_t i = 0; i < count; ++i) {
        result += s;
    }
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::PadStart(int64_t targetLength, TsString* padString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    int32_t len = s.length();
    if (targetLength <= len) return this;
    
    icu::UnicodeString pad;
    if (padString->isSmall) pad = icu::UnicodeString::fromUTF8(padString->data.inlineBuffer);
    else pad = *static_cast<icu::UnicodeString*>(padString->data.heap.impl);

    if (pad.length() == 0) return this;
    
    icu::UnicodeString result;
    int32_t padLen = (int32_t)targetLength - len;
    while (result.length() < padLen) {
        result += pad;
    }
    if (result.length() > padLen) {
        result.truncate(padLen);
    }
    result += s;
    
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::PadEnd(int64_t targetLength, TsString* padString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    int32_t len = s.length();
    if (targetLength <= len) return this;
    
    icu::UnicodeString pad;
    if (padString->isSmall) pad = icu::UnicodeString::fromUTF8(padString->data.inlineBuffer);
    else pad = *static_cast<icu::UnicodeString*>(padString->data.heap.impl);

    if (pad.length() == 0) return this;
    
    icu::UnicodeString result = s;
    int32_t padLen = (int32_t)targetLength - len;
    icu::UnicodeString padding;
    while (padding.length() < padLen) {
        padding += pad;
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
    if (isSmall && prefix->isSmall) {
        if (prefix->length > length) return false;
        return std::memcmp(data.inlineBuffer, prefix->data.inlineBuffer, prefix->length) == 0;
    }

    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString p;
    if (prefix->isSmall) p = icu::UnicodeString::fromUTF8(prefix->data.inlineBuffer);
    else p = *static_cast<icu::UnicodeString*>(prefix->data.heap.impl);

    return s.startsWith(p);
}

bool TsString::Includes(TsString* searchString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString search;
    if (searchString->isSmall) search = icu::UnicodeString::fromUTF8(searchString->data.inlineBuffer);
    else search = *static_cast<icu::UnicodeString*>(searchString->data.heap.impl);

    return s.indexOf(search) != -1;
}

int64_t TsString::IndexOf(TsString* searchString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString search;
    if (searchString->isSmall) search = icu::UnicodeString::fromUTF8(searchString->data.inlineBuffer);
    else search = *static_cast<icu::UnicodeString*>(searchString->data.heap.impl);

    return s.indexOf(search);
}

TsString* TsString::ToLowerCase() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    s.toLower();
    std::string str;
    s.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::ToUpperCase() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    s.toUpper();
    std::string str;
    s.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::Replace(TsString* pattern, TsString* replacement) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString p;
    if (pattern->isSmall) p = icu::UnicodeString::fromUTF8(pattern->data.inlineBuffer);
    else p = *static_cast<icu::UnicodeString*>(pattern->data.heap.impl);

    icu::UnicodeString r;
    if (replacement->isSmall) r = icu::UnicodeString::fromUTF8(replacement->data.inlineBuffer);
    else r = *static_cast<icu::UnicodeString*>(replacement->data.heap.impl);
    
    int32_t pos = s.indexOf(p);
    if (pos != -1) {
        s.replace(pos, p.length(), r);
    }
    
    std::string str;
    s.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::ReplaceAll(TsString* pattern, TsString* replacement) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString p;
    if (pattern->isSmall) p = icu::UnicodeString::fromUTF8(pattern->data.inlineBuffer);
    else p = *static_cast<icu::UnicodeString*>(pattern->data.heap.impl);

    icu::UnicodeString r;
    if (replacement->isSmall) r = icu::UnicodeString::fromUTF8(replacement->data.inlineBuffer);
    else r = *static_cast<icu::UnicodeString*>(replacement->data.heap.impl);
    
    s.findAndReplace(p, r);
    
    std::string str;
    s.toUTF8String(str);
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
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    matcher->reset(s);
    if (matcher->find()) {
        UErrorCode status = U_ZERO_ERROR;
        return matcher->start(status);
    }
    return -1;
}

TsString* TsString::Replace(TsRegExp* regexp, TsString* replacement) {
    if (!regexp || !replacement) return this;
    
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString r;
    if (replacement->isSmall) r = icu::UnicodeString::fromUTF8(replacement->data.inlineBuffer);
    else r = *static_cast<icu::UnicodeString*>(replacement->data.heap.impl);
    
    UErrorCode status = U_ZERO_ERROR;
    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    matcher->reset(s);
    
    icu::UnicodeString result;
    if (regexp->GetFlags()->Includes(TsString::Create("g"))) {
        result = matcher->replaceAll(r, status);
    } else {
        result = matcher->replaceFirst(r, status);
    }
    
    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

void* TsString::Split(TsRegExp* regexp) {
    if (!regexp) return Split(TsString::Create(""));
    
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s = *static_cast<icu::UnicodeString*>(data.heap.impl);

    UErrorCode status = U_ZERO_ERROR;
    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    matcher->reset(s);
    
    TsArray* arr = TsArray::Create();
    int32_t start = 0;
    
    while (matcher->find()) {
        int32_t matchStart = matcher->start(status);
        int32_t matchEnd = matcher->end(status);
        
        icu::UnicodeString sub = s.tempSubString(start, matchStart - start);
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
    icu::UnicodeString sub = s.tempSubString(start);
    std::string utf8;
    sub.toUTF8String(utf8);
    arr->Push((int64_t)ts_value_make_string(TsString::Create(utf8.c_str())));
    
    return arr;
}

bool TsString::Equals(TsString* other) {
    if (this == other) return true;
    if (!other) return false;
    if (length != other->length) return false;
    if (isSmall && other->isSmall) {
        return std::memcmp(data.inlineBuffer, other->data.inlineBuffer, length) == 0;
    }
    
    icu::UnicodeString s1;
    if (isSmall) s1 = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else s1 = *static_cast<icu::UnicodeString*>(data.heap.impl);

    icu::UnicodeString s2;
    if (other->isSmall) s2 = icu::UnicodeString::fromUTF8(other->data.inlineBuffer);
    else s2 = *static_cast<icu::UnicodeString*>(other->data.heap.impl);

    return s1 == s2;
}

extern "C" {
    void* ts_int_to_string(int64_t value, int64_t radix) {
        if (radix == 10) {
            return TsString::FromInt(value);
        }
        if (radix < 2 || radix > 36) radix = 10;
        
        char buf[128];
        const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
        
        char* p = buf + 127;
        *p = '\0';
        bool negative = value < 0;
        uint64_t uval = negative ? (uint64_t)-value : (uint64_t)value;
        
        do {
            *--p = digits[uval % radix];
            uval /= radix;
        } while (uval > 0);
        
        if (negative) *--p = '-';
        return TsString::Create(p);
    }

    void* ts_double_to_string(double value, int64_t radix) {
        if (radix == 10) {
            return TsString::FromDouble(value);
        }
        if (radix < 2 || radix > 36) radix = 10;
        
        if (std::isnan(value)) return TsString::Create("NaN");
        if (std::isinf(value)) return TsString::Create(value < 0 ? "-Infinity" : "Infinity");

        if (std::floor(value) == value) {
            return ts_int_to_string((int64_t)value, radix);
        }

        return TsString::FromDouble(value);
    }

    void* ts_number_to_string(double value, int64_t radix) {
        return ts_double_to_string(value, radix);
    }

    void* ts_double_to_fixed(double value, int64_t fractionDigits) {
        if (fractionDigits < 0) fractionDigits = 0;
        if (fractionDigits > 100) fractionDigits = 100;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.*f", (int)fractionDigits, value);
        return TsString::Create(buf);
    }

    void* ts_number_to_fixed(double value, int64_t fractionDigits) {
        return ts_double_to_fixed(value, fractionDigits);
    }

    void* ts_string_create(const char* str) {
        TsString* res = TsString::Create(str);
        return (void*)res;
    }

    int64_t ts_string_length(void* str) {
        if (!str) return 0;
        return ((TsString*)str)->Length();
    }

    int64_t ts_string_charCodeAt(void* str, int64_t index) {
        return ((TsString*)str)->CharCodeAt(index);
    }

    void* ts_string_charAt(void* str, int64_t index) {
        return ((TsString*)str)->CharAt(index);
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
        if (!val) return TsString::GetInterned("undefined");
        
        // Check for raw TsString
        uint32_t magic = *(uint32_t*)val;
        if (magic == 0x53545247) { // TsString::MAGIC
            return val;
        }

        switch (val->type) {
            case ValueType::UNDEFINED: return TsString::GetInterned("undefined");
            case ValueType::NUMBER_INT: return TsString::FromInt(val->i_val);
            case ValueType::NUMBER_DBL: return TsString::FromDouble(val->d_val);
            case ValueType::BOOLEAN: return TsString::FromBool(val->b_val);
            case ValueType::STRING_PTR: return val->ptr_val;
            case ValueType::OBJECT_PTR: return TsString::GetInterned("[object Object]");
            case ValueType::ARRAY_PTR: return TsString::GetInterned("[object Array]");
            case ValueType::PROMISE_PTR: return TsString::GetInterned("[object Promise]");
            default: return TsString::GetInterned("unknown");
        }
    }
}
