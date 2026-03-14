#include "TsString.h"
#include "TsConsString.h"
#include "TsArray.h"
#include "TsRegExp.h"
#include "TsRuntime.h"
#include "GC.h"
#include "TsGC.h"
#include "TsNanBox.h"
#include <cmath>
#include <unicode/unistr.h>
#include <unicode/regex.h>
#include <unicode/normlzr.h>
#include <new>
#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>

// Forward declarations for calling functions (defined in TsObject.cpp)
extern "C" {
    TsValue* ts_call_1(TsValue* func, TsValue* a1);
    TsValue* ts_call_2(TsValue* func, TsValue* a1, TsValue* a2);
    TsValue* ts_call_3(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3);
    TsValue* ts_call_4(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4);
    TsValue* ts_call_5(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5);
    TsValue* ts_call_6(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5, TsValue* a6);
    TsValue* ts_call_7(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5, TsValue* a6, TsValue* a7);
    TsValue* ts_call_8(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8);
    TsValue* ts_call_9(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9);
    TsValue* ts_call_10(TsValue* func, TsValue* a1, TsValue* a2, TsValue* a3, TsValue* a4, TsValue* a5, TsValue* a6, TsValue* a7, TsValue* a8, TsValue* a9, TsValue* a10);
    void* ts_string_from_value(TsValue* val);
}

// Check if a NaN-boxed TsValue is a callable function (closure or TsFunction)
static bool ts_nanbox_is_callable(void* val) {
    if (!val) return false;
    uint64_t nb = nanbox_from_tsvalue_ptr((TsValue*)val);
    if (!nanbox_is_ptr(nb)) return false;
    void* ptr = nanbox_to_ptr(nb);
    if (!ptr) return false;
    uint32_t magic16 = *(uint32_t*)((char*)ptr + 16);
    if (magic16 == 0x434C5352) return true; // TsClosure::MAGIC "CLSR"
    if (magic16 == 0x46554E43) return true; // TsFunction::MAGIC "FUNC"
    return false;
}

// Call a function with a variable number of TsValue* args
static TsValue* ts_call_variadic_impl(TsValue* fn, TsValue** args, int count) {
    TsValue* u = ts_value_make_undefined();
    switch (count) {
        case 0: return ts_call_1(fn, u);
        case 1: return ts_call_1(fn, args[0]);
        case 2: return ts_call_2(fn, args[0], args[1]);
        case 3: return ts_call_3(fn, args[0], args[1], args[2]);
        case 4: return ts_call_4(fn, args[0], args[1], args[2], args[3]);
        case 5: return ts_call_5(fn, args[0], args[1], args[2], args[3], args[4]);
        case 6: return ts_call_6(fn, args[0], args[1], args[2], args[3], args[4], args[5]);
        case 7: return ts_call_7(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
        case 8: return ts_call_8(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
        case 9: return ts_call_9(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
        default: return ts_call_10(fn, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    }
}

// CRITICAL FIX: Cache for common numeric strings (0-999)
// Lodash does thousands of numeric property accesses which were allocating new strings each time
static std::unordered_map<int64_t, TsString*> numericStringCache;
static const int MAX_CACHED_INT = 9999;  // Expanded from 999
static const int MIN_CACHED_INT = -100;  // Cache negative numbers too

// OPTIMIZATION: Global string interning pool
// Caches ALL strings to eliminate duplicate allocations during property access
static std::unordered_map<std::string, TsString*> globalStringCache;
static const size_t MAX_INTERN_SIZE = 256; // Only intern strings <= 256 chars
static const size_t MAX_CACHE_SIZE = 100000; // Allow more interned strings for Map-heavy workloads

// Cache for non-integer doubles (was function-local, moved to file scope for GC scanning)
static std::unordered_map<double, TsString*> doubleStringCache;
static const size_t MAX_DOUBLE_CACHE = 1000;

// GC scanner for string caches: called during mark phase to keep cached strings alive.
// Without this, the GC has no way to find these TsString* pointers since they're in
// C++ static unordered_maps that the conservative stack scanner can't see.
static bool g_string_cache_scanner_registered = false;
static void ts_string_cache_gc_scanner(void* /*context*/) {
    for (auto& pair : numericStringCache) {
        if (pair.second) ts_gc_mark_object((void*)pair.second);
    }
    for (auto& pair : globalStringCache) {
        if (pair.second) ts_gc_mark_object((void*)pair.second);
    }
    for (auto& pair : doubleStringCache) {
        if (pair.second) ts_gc_mark_object((void*)pair.second);
    }
}

static void ensureStringCacheScannerRegistered() {
    if (!g_string_cache_scanner_registered) {
        g_string_cache_scanner_registered = true;
        ts_gc_register_scanner(ts_string_cache_gc_scanner, nullptr);
    }
}

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

// Create a TsString directly in old-gen (bypasses nursery).
// Used for cached/interned strings which are long-lived and stored in
// global caches that are invisible to the nursery GC's card table.
TsString* TsString::CreateInOldGen(const char* utf8Str) {
    if (!utf8Str) utf8Str = "";
    size_t len = std::strlen(utf8Str);

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

    void* mem = ts_gc_alloc_old_gen(sizeof(TsString));
    if (isAscii) {
        return new(mem) TsString(utf8Str, (uint32_t)len);
    } else {
        // Constructor allocates ICU string via ts_alloc (may go to nursery).
        // Since TsString is in old-gen, we need a write barrier so minor GC
        // knows this old-gen object references a nursery pointer.
        TsString* str = new(mem) TsString(utf8Str);
        ts_gc_write_barrier(&str->data.heap.impl, str->data.heap.impl);
        return str;
    }
}

TsString* TsString::CreateFromAsciiBuffer(char* buf, uint32_t len) {
    void* mem = ts_alloc(sizeof(TsString));
    TsString* str = new(mem) TsString();
    str->magic = MAGIC;
    str->length = len;
    str->isSmall = false;
    str->hashComputed = false;
    str->cachedHash = 0;
    str->data.heap.impl = nullptr;
    str->data.heap.utf8Buffer = buf;
    return str;
}

TsString* TsString::GetInterned(const char* utf8Str) {
    if (!utf8Str) return Create(""); // Empty string fallback

    ensureStringCacheScannerRegistered();

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

    // Cache miss - create in old-gen (cached strings are long-lived,
    // and globalStringCache is in malloc'd memory invisible to card table)
    TsString* str = CreateInOldGen(utf8Str);

    // Add to cache if not too large
    if (globalStringCache.size() < MAX_CACHE_SIZE) {
        globalStringCache[key] = str;
    }

    return str;
}

TsString* TsString::FindInterned(const char* utf8Str) {
    if (!utf8Str) return nullptr;
    size_t len = std::strlen(utf8Str);
    if (len > MAX_INTERN_SIZE) return nullptr;
    std::string key(utf8Str, len);
    auto it = globalStringCache.find(key);
    if (it != globalStringCache.end()) return it->second;
    return nullptr;
}

TsString::TsString(const char* utf8Str, uint32_t len) {
    magic = MAGIC;
    isSmall = true;
    hashComputed = false;
    cachedHash = 0;
    length = len;
    std::memcpy(data.inlineBuffer, utf8Str, len);
    if (len < 64) data.inlineBuffer[len] = '\0';
}

TsString::TsString(const char* utf8Str) {
    magic = MAGIC;
    isSmall = false;
    hashComputed = false;
    cachedHash = 0;
    // Allocate the ICU string on the GC heap as well
    void* mem = ts_alloc(sizeof(icu::UnicodeString));
    // Must use fromUTF8 to properly interpret the UTF-8 encoding
    // The regular constructor treats input as default system encoding, not UTF-8!
    data.heap.impl = new(mem) icu::UnicodeString(icu::UnicodeString::fromUTF8(utf8Str));
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

uint32_t TsString::Hash() {
    if (hashComputed) return cachedHash;
    const char* str = ToUtf8();
    if (!str) { cachedHash = 0; hashComputed = true; return 0; }
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        h = ((h << 5) + h) + c;
    cachedHash = h;
    hashComputed = true;
    return h;
}

TsString* TsString::Concat(TsString* a, TsString* b) {
    if (!a || !b) return nullptr; // Safety check

    // Fast path: both small ASCII, result fits in inlineBuffer (< 64 bytes)
    if (a->isSmall && b->isSmall) {
        uint32_t totalLen = a->length + b->length;
        if (totalLen < 64) {
            void* mem = ts_alloc(sizeof(TsString));
            TsString* result = new(mem) TsString();
            result->magic = MAGIC;
            result->isSmall = true;
            result->hashComputed = false;
            result->cachedHash = 0;
            result->length = totalLen;
            std::memcpy(result->data.inlineBuffer, a->data.inlineBuffer, a->length);
            std::memcpy(result->data.inlineBuffer + a->length, b->data.inlineBuffer, b->length);
            result->data.inlineBuffer[totalLen] = '\0';
            return result;
        }
    }

    // Large string concat: create a rope node instead of copying
    return (TsString*)TsConsString::Create(a, b);
}

TsString* TsString::FromInt(int64_t value) {
    ensureStringCacheScannerRegistered();

    // Check cache for common values (0-999)
    if (value >= MIN_CACHED_INT && value <= MAX_CACHED_INT) {
        auto it = numericStringCache.find(value);
        if (it != numericStringCache.end()) {
            return it->second;
        }

        // Not in cache, create in old-gen and cache it
        // (numericStringCache is in malloc'd memory, invisible to card table)
        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), "%lld", value);
        TsString* str = CreateInOldGen(buf);
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

    ensureStringCacheScannerRegistered();

    // Cache common non-integer doubles
    auto it = doubleStringCache.find(value);
    if (it != doubleStringCache.end()) {
        return it->second;
    }

    char buf[64];
    int len = std::snprintf(buf, sizeof(buf), "%g", value);

    // Create in old-gen if caching (doubleStringCache is in malloc'd memory)
    TsString* str;
    if (doubleStringCache.size() < MAX_DOUBLE_CACHE) {
        str = CreateInOldGen(buf);
        doubleStringCache[value] = str;
    } else {
        str = Create(buf);
    }

    return str;
}

int64_t TsString::Length() {
    return length;
}

int64_t TsString::CharCodeAt(int64_t index) {
    if (index < 0 || index >= length) return 0;
    if (isSmall) return (unsigned char)data.inlineBuffer[index];

    // For ASCII heap strings (from CreateFromAsciiBuffer), use buffer directly
    if (!data.heap.impl && data.heap.utf8Buffer) {
        return (unsigned char)data.heap.utf8Buffer[index];
    }
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    return s->charAt((int32_t)index);
}

int64_t TsString::CodePointAt(int64_t index) {
    if (index < 0 || index >= length) return -1;  // undefined for out of bounds
    if (isSmall) {
        return (unsigned char)data.inlineBuffer[index];
    }
    if (!data.heap.impl && data.heap.utf8Buffer) {
        return (unsigned char)data.heap.utf8Buffer[index];
    }
    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    // char32At returns the full Unicode code point, handling surrogate pairs
    return s->char32At((int32_t)index);
}

TsString* TsString::FromCodePoint(int64_t* codePoints, int64_t count) {
    icu::UnicodeString result;
    for (int64_t i = 0; i < count; i++) {
        int64_t cp = codePoints[i];
        if (cp < 0 || cp > 0x10FFFF) {
            // Invalid code point - skip
            continue;
        }
        result.append((UChar32)cp);
    }
    std::string utf8;
    result.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}


TsString* TsString::CharAt(int64_t index) {
    if (index < 0 || index >= length) return TsString::Create("");
    if (isSmall) {
        char buf[2] = { data.inlineBuffer[index], 0 };
        return TsString::Create(buf);
    }

    // ASCII heap string (from CreateFromAsciiBuffer) - use buffer directly
    if (!data.heap.impl && data.heap.utf8Buffer) {
        char buf[2] = { data.heap.utf8Buffer[index], 0 };
        return TsString::Create(buf);
    }

    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    icu::UnicodeString charStr = s->tempSubString((int32_t)index, 1);
    std::string utf8;
    charStr.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::At(int64_t index) {
    // at() supports negative indices (relative to end)
    if (index < 0) {
        index = length + index;
    }
    // Out of bounds returns undefined (represented as empty string for now)
    if (index < 0 || index >= length) return nullptr;
    if (isSmall) {
        char buf[2] = { data.inlineBuffer[index], 0 };
        return TsString::Create(buf);
    }

    // ASCII heap string (from CreateFromAsciiBuffer) - use buffer directly
    if (!data.heap.impl && data.heap.utf8Buffer) {
        char buf[2] = { data.heap.utf8Buffer[index], 0 };
        return TsString::Create(buf);
    }

    icu::UnicodeString* s = static_cast<icu::UnicodeString*>(data.heap.impl);
    icu::UnicodeString charStr = s->tempSubString((int32_t)index, 1);
    std::string utf8;
    charStr.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

// Lazily ensure the ICU UnicodeString is created for heap strings.
// Called when data.heap.impl is nullptr (e.g., CreateFromAsciiBuffer).
void TsString::ensureImpl() {
    if (!isSmall && !data.heap.impl && data.heap.utf8Buffer) {
        void* mem = ts_alloc(sizeof(icu::UnicodeString));
        data.heap.impl = new(mem) icu::UnicodeString(icu::UnicodeString::fromUTF8(data.heap.utf8Buffer));
    }
}

icu::UnicodeString TsString::getUStr() {
    if (isSmall) return icu::UnicodeString::fromUTF8(data.inlineBuffer);
    ensureImpl();
    return *static_cast<icu::UnicodeString*>(data.heap.impl);
}

icu::UnicodeString TsString::ToUnicodeString() const {
    if (isSmall) {
        return icu::UnicodeString::fromUTF8(data.inlineBuffer);
    }
    if (!data.heap.impl) {
        const_cast<TsString*>(this)->ensureImpl();
    }
    return *static_cast<icu::UnicodeString*>(data.heap.impl);
}

void* TsString::Split(TsString* separator) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString sep;
    if (separator->isSmall) sep = icu::UnicodeString::fromUTF8(separator->data.inlineBuffer);
    else { separator->ensureImpl(); sep = *static_cast<icu::UnicodeString*>(separator->data.heap.impl); }
    
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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    s.trim();

    std::string utf8;
    s.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::TrimStart() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    // Find first non-whitespace character
    int32_t start = 0;
    int32_t len = s.length();
    while (start < len && u_isWhitespace(s.charAt(start))) {
        start++;
    }

    if (start == 0) return this; // No leading whitespace

    s.remove(0, start);

    std::string utf8;
    s.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::TrimEnd() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    // Find last non-whitespace character
    int32_t end = s.length();
    while (end > 0 && u_isWhitespace(s.charAt(end - 1))) {
        end--;
    }

    if (end == s.length()) return this; // No trailing whitespace

    s.truncate(end);

    std::string utf8;
    s.toUTF8String(utf8);
    return TsString::Create(utf8.c_str());
}

TsString* TsString::Substring(int64_t start, int64_t end) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    int32_t len = s.length();
    if (targetLength <= len) return this;

    // Default to space if padString is null (per JS spec)
    icu::UnicodeString pad;
    if (!padString) {
        pad = " ";
    } else if (padString->isSmall) {
        pad = icu::UnicodeString::fromUTF8(padString->data.inlineBuffer);
    } else {
        padString->ensureImpl(); pad = *static_cast<icu::UnicodeString*>(padString->data.heap.impl);
    }

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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    int32_t len = s.length();
    if (targetLength <= len) return this;

    // Default to space if padString is null (per JS spec)
    icu::UnicodeString pad;
    if (!padString) {
        pad = " ";
    } else if (padString->isSmall) {
        pad = icu::UnicodeString::fromUTF8(padString->data.inlineBuffer);
    } else {
        padString->ensureImpl(); pad = *static_cast<icu::UnicodeString*>(padString->data.heap.impl);
    }

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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString p;
    if (prefix->isSmall) p = icu::UnicodeString::fromUTF8(prefix->data.inlineBuffer);
    else { prefix->ensureImpl(); p = *static_cast<icu::UnicodeString*>(prefix->data.heap.impl); }

    return s.startsWith(p);
}

bool TsString::EndsWith(TsString* suffix) {
    if (isSmall && suffix->isSmall) {
        if (suffix->length > length) return false;
        return std::memcmp(data.inlineBuffer + length - suffix->length, suffix->data.inlineBuffer, suffix->length) == 0;
    }

    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString p;
    if (suffix->isSmall) p = icu::UnicodeString::fromUTF8(suffix->data.inlineBuffer);
    else { suffix->ensureImpl(); p = *static_cast<icu::UnicodeString*>(suffix->data.heap.impl); }

    return s.endsWith(p);
}

bool TsString::Includes(TsString* searchString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString search;
    if (searchString->isSmall) search = icu::UnicodeString::fromUTF8(searchString->data.inlineBuffer);
    else { searchString->ensureImpl(); search = *static_cast<icu::UnicodeString*>(searchString->data.heap.impl); }

    return s.indexOf(search) != -1;
}

int64_t TsString::IndexOf(TsString* searchString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString search;
    if (searchString->isSmall) search = icu::UnicodeString::fromUTF8(searchString->data.inlineBuffer);
    else { searchString->ensureImpl(); search = *static_cast<icu::UnicodeString*>(searchString->data.heap.impl); }

    return s.indexOf(search);
}

int64_t TsString::LastIndexOf(TsString* searchString) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString search;
    if (searchString->isSmall) search = icu::UnicodeString::fromUTF8(searchString->data.inlineBuffer);
    else { searchString->ensureImpl(); search = *static_cast<icu::UnicodeString*>(searchString->data.heap.impl); }

    return s.lastIndexOf(search);
}

TsString* TsString::ToLowerCase() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    s.toLower();
    std::string str;
    s.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::ToUpperCase() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    s.toUpper();
    std::string str;
    s.toUTF8String(str);
    return Create(str.c_str());
}

TsString* TsString::Normalize(TsString* form) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    // Default to NFC if no form specified
    UNormalizationMode mode = UNORM_NFC;

    if (form) {
        const char* formStr = form->ToUtf8();
        if (strcmp(formStr, "NFD") == 0) {
            mode = UNORM_NFD;
        } else if (strcmp(formStr, "NFKC") == 0) {
            mode = UNORM_NFKC;
        } else if (strcmp(formStr, "NFKD") == 0) {
            mode = UNORM_NFKD;
        }
        // NFC is default, no else needed
    }

    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString result;
    icu::Normalizer::normalize(s, mode, 0, result, status);

    if (U_FAILURE(status)) {
        // On error, return original string
        return this;
    }

    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

// ES2024: Check if string is well-formed (no lone surrogates)
bool TsString::IsWellFormed() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    int32_t len = s.length();
    for (int32_t i = 0; i < len; i++) {
        UChar c = s.charAt(i);

        // Check for high surrogate (U+D800-U+DBFF)
        if (U16_IS_LEAD(c)) {
            // Must be followed by a low surrogate
            if (i + 1 >= len || !U16_IS_TRAIL(s.charAt(i + 1))) {
                return false;  // Lone high surrogate
            }
            i++;  // Skip the low surrogate we just checked
        }
        // Check for low surrogate (U+DC00-U+DFFF)
        else if (U16_IS_TRAIL(c)) {
            return false;  // Lone low surrogate (not preceded by high surrogate)
        }
    }
    return true;
}

// ES2024: Replace lone surrogates with U+FFFD (replacement character)
TsString* TsString::ToWellFormed() {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString result;
    int32_t len = s.length();

    for (int32_t i = 0; i < len; i++) {
        UChar c = s.charAt(i);

        // Check for high surrogate (U+D800-U+DBFF)
        if (U16_IS_LEAD(c)) {
            // Check if followed by a low surrogate
            if (i + 1 < len && U16_IS_TRAIL(s.charAt(i + 1))) {
                // Valid surrogate pair - keep both
                result.append(c);
                result.append(s.charAt(i + 1));
                i++;  // Skip the low surrogate
            } else {
                // Lone high surrogate - replace with U+FFFD
                result.append((UChar)0xFFFD);
            }
        }
        // Check for low surrogate (U+DC00-U+DFFF)
        else if (U16_IS_TRAIL(c)) {
            // Lone low surrogate - replace with U+FFFD
            result.append((UChar)0xFFFD);
        }
        else {
            // Regular character - keep as is
            result.append(c);
        }
    }

    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

TsString* TsString::Replace(TsString* pattern, TsString* replacement) {
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString p;
    if (pattern->isSmall) p = icu::UnicodeString::fromUTF8(pattern->data.inlineBuffer);
    else { pattern->ensureImpl(); p = *static_cast<icu::UnicodeString*>(pattern->data.heap.impl); }

    icu::UnicodeString r;
    if (replacement->isSmall) r = icu::UnicodeString::fromUTF8(replacement->data.inlineBuffer);
    else { replacement->ensureImpl(); r = *static_cast<icu::UnicodeString*>(replacement->data.heap.impl); }
    
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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString p;
    if (pattern->isSmall) p = icu::UnicodeString::fromUTF8(pattern->data.inlineBuffer);
    else { pattern->ensureImpl(); p = *static_cast<icu::UnicodeString*>(pattern->data.heap.impl); }

    icu::UnicodeString r;
    if (replacement->isSmall) r = icu::UnicodeString::fromUTF8(replacement->data.inlineBuffer);
    else { replacement->ensureImpl(); r = *static_cast<icu::UnicodeString*>(replacement->data.heap.impl); }
    
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

// ES2020: matchAll returns an array of all match results (each with groups, index, input)
void* TsString::MatchAll(TsRegExp* regexp) {
    if (!regexp) return nullptr;

    // matchAll requires global flag - in strict mode, throw TypeError
    // For now, we'll just set global behavior regardless

    TsArray* results = TsArray::Create();
    regexp->SetLastIndex(0);

    while (true) {
        void* execResult = regexp->Exec(this);
        if (!execResult) break;

        // Each result is a full match array from exec (with index, input, groups)
        results->Push((int64_t)execResult);
    }

    // Reset lastIndex after iteration
    regexp->SetLastIndex(0);

    return results;
}

int64_t TsString::Search(TsRegExp* regexp) {
    if (!regexp) return -1;
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

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
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString r;
    if (replacement->isSmall) r = icu::UnicodeString::fromUTF8(replacement->data.inlineBuffer);
    else { replacement->ensureImpl(); r = *static_cast<icu::UnicodeString*>(replacement->data.heap.impl); }

    // Translate JavaScript replacement patterns to ICU equivalents.
    // JS: \ is literal, $& = whole match, $$ = literal $, $1-$9 = groups
    // ICU: \ is escape char, $0 = whole match, \$ = literal $, $1-$9 = groups
    {
        icu::UnicodeString translated;
        for (int32_t i = 0; i < r.length(); ++i) {
            UChar ch = r[i];
            if (ch == 0x5C /* \ */) {
                // In JS, backslash is literal. In ICU, \ is escape char.
                // Emit \\ so ICU treats it as a literal backslash.
                translated.append((UChar)0x5C);
                translated.append((UChar)0x5C);
            } else if (ch == 0x24 /* $ */ && i + 1 < r.length()) {
                UChar next = r[i + 1];
                if (next == 0x26 /* & */) {
                    // JS $& (whole match) -> ICU $0
                    translated.append(icu::UnicodeString("$0"));
                    ++i;
                } else if (next == 0x24 /* $ */) {
                    // JS $$ (literal $) -> ICU \$
                    translated.append((UChar)0x5C);
                    translated.append((UChar)0x24);
                    ++i;
                } else {
                    // $1-$9 etc. pass through (same in JS and ICU)
                    translated.append(ch);
                }
            } else {
                translated.append(ch);
            }
        }
        r = translated;
    }

    UErrorCode status = U_ZERO_ERROR;
    icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
    if (!matcher) return this;
    matcher->reset(s);

    icu::UnicodeString result;
    TsString* flags = regexp->GetFlags();
    if (flags && flags->Includes(TsString::Create("g"))) {
        result = matcher->replaceAll(r, status);
    } else {
        result = matcher->replaceFirst(r, status);
    }

    if (U_FAILURE(status)) return this;

    std::string utf8;
    result.toUTF8String(utf8);
    return Create(utf8.c_str());
}

void* TsString::Split(TsRegExp* regexp) {
    if (!regexp) return Split(TsString::Create(""));
    
    icu::UnicodeString s;
    if (isSmall) s = icu::UnicodeString::fromUTF8(data.inlineBuffer);
    else { ensureImpl(); s = *static_cast<icu::UnicodeString*>(data.heap.impl); }

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
    else { ensureImpl(); s1 = *static_cast<icu::UnicodeString*>(data.heap.impl); }

    icu::UnicodeString s2;
    if (other->isSmall) s2 = icu::UnicodeString::fromUTF8(other->data.inlineBuffer);
    else { other->ensureImpl(); s2 = *static_cast<icu::UnicodeString*>(other->data.heap.impl); }

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
        // Fast path: return length without flattening for cons strings
        return ts_string_like_length(str);
    }

    int64_t ts_string_charCodeAt(void* str, int64_t index) {
        if (!str) return 0;
        TsString* s = ts_ensure_flat(str);
        if (!s) return 0;
        return s->CharCodeAt(index);
    }

    int64_t ts_string_codePointAt(void* str, int64_t index) {
        if (!str) return -1;
        TsString* s = ts_ensure_flat(str);
        if (!s) return -1;
        return s->CodePointAt(index);
    }

    void* ts_string_fromCodePoint(void* codePointsArray) {
        if (!codePointsArray) return TsString::Create("");
        TsArray* arr = (TsArray*)codePointsArray;
        int64_t len = arr->Length();
        int64_t* codePoints = (int64_t*)ts_alloc(len * sizeof(int64_t));
        for (int64_t i = 0; i < len; i++) {
            int64_t rawVal = arr->Get(i);
            // Array elements are NaN-boxed values
            uint64_t nb = (uint64_t)rawVal;
            codePoints[i] = nanbox_to_int64(nb);
        }
        return TsString::FromCodePoint(codePoints, len);
    }

    void* ts_string_raw(void* templateObj, void* substitutionsArray) {
        // String.raw(template, ...substitutions)
        // template is an object with a 'raw' property that contains array of string pieces
        if (!templateObj) return TsString::Create("");

        // Get the 'raw' property from the template object
        void* rawArray = ts_object_get_property(templateObj, "raw");

        // Unbox if it's a NaN-boxed pointer
        uint64_t rawNb = (uint64_t)(uintptr_t)rawArray;
        if (nanbox_is_ptr(rawNb)) {
            rawArray = nanbox_to_ptr(rawNb);
        }

        if (!rawArray || rawNb < 0x10000) return TsString::Create("");  // Guard NaN-boxed specials

        TsArray* rawPieces = (TsArray*)rawArray;
        TsArray* subs = substitutionsArray ? (TsArray*)substitutionsArray : nullptr;

        int64_t rawLen = rawPieces->Length();
        if (rawLen == 0) return TsString::Create("");

        // Build the result by interleaving raw pieces and substitutions
        std::string result;

        for (int64_t i = 0; i < rawLen; i++) {
            // Get raw string piece
            int64_t pieceVal = rawPieces->Get(i);
            uint64_t pieceNb = (uint64_t)pieceVal;
            TsString* piece = nullptr;

            if (nanbox_is_ptr(pieceNb)) {
                void* pp = nanbox_to_ptr(pieceNb);
                if (pp) {
                    uint32_t m = *(uint32_t*)pp;
                    if (m == 0x53545247) piece = (TsString*)pp; // TsString
                    else if (m == TsConsString::MAGIC) piece = ((TsConsString*)pp)->Flatten();
                }
            }

            if (piece) {
                result += piece->ToUtf8();
            }

            // Add substitution if available (there's one fewer substitution than raw pieces)
            if (subs && i < rawLen - 1 && i < subs->Length()) {
                int64_t subVal = subs->Get(i);
                uint64_t subNb = (uint64_t)subVal;

                if (nanbox_is_undefined(subNb)) {
                    result += "undefined";
                } else if (nanbox_is_null(subNb)) {
                    result += "null";
                } else if (nanbox_is_true(subNb)) {
                    result += "true";
                } else if (nanbox_is_false(subNb)) {
                    result += "false";
                } else if (nanbox_is_int32(subNb)) {
                    result += std::to_string(nanbox_to_int32(subNb));
                } else if (nanbox_is_double(subNb)) {
                    result += std::to_string(nanbox_to_double(subNb));
                } else if (nanbox_is_ptr(subNb)) {
                    void* sp = nanbox_to_ptr(subNb);
                    if (!sp) {
                        result += "null";
                    } else {
                        uint32_t sm = *(uint32_t*)sp;
                        if (sm == 0x53545247) {
                            result += ((TsString*)sp)->ToUtf8();
                        } else if (sm == TsConsString::MAGIC) {
                            result += ((TsConsString*)sp)->Flatten()->ToUtf8();
                        } else {
                            result += "[object]";
                        }
                    }
                }
            }
        }

        return TsString::Create(result.c_str());
    }

    void* ts_string_charAt(void* str, int64_t index) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return TsString::Create("");
        return s->CharAt(index);
    }

    void* ts_string_at(void* str, int64_t index) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return ts_value_make_undefined();
        TsString* result = s->At(index);
        if (!result) return ts_value_make_undefined();
        return result;
    }

    void* ts_string_concat(void* a, void* b) {
        // Fast path: both args have valid string magic (common case)
        if (a && b) {
            uint32_t magicA = *(uint32_t*)a;
            uint32_t magicB = *(uint32_t*)b;
            if ((magicA == TsString::MAGIC || magicA == TsConsString::MAGIC) &&
                (magicB == TsString::MAGIC || magicB == TsConsString::MAGIC)) {
                // Both TsString? Use Concat (handles small-string fast path)
                if (magicA == TsString::MAGIC && magicB == TsString::MAGIC) {
                    return TsString::Concat((TsString*)a, (TsString*)b);
                }
                return TsConsString::Create(a, b);
            }
        }

        // Slow path: validate/convert non-string arguments
        auto ensureStringLike = [](void* ptr) -> void* {
            if (!ptr) return TsString::Create("");
            uintptr_t addr = (uintptr_t)ptr;
            if (addr < 0x10000) return TsString::Create("");
            uint32_t magic = *(uint32_t*)ptr;
            if (magic == TsString::MAGIC || magic == TsConsString::MAGIC) return ptr;
            uint8_t firstByte = *(uint8_t*)ptr;
            if (firstByte <= 10) {
                TsString* result = (TsString*)ts_string_from_value((TsValue*)ptr);
                if (result) return result;
            }
            return TsString::Create("");
        };
        void* strA = ensureStringLike(a);
        void* strB = ensureStringLike(b);
        if (*(uint32_t*)strA == TsString::MAGIC && *(uint32_t*)strB == TsString::MAGIC) {
            return TsString::Concat((TsString*)strA, (TsString*)strB);
        }
        return TsConsString::Create(strA, strB);
    }

    void* ts_string_split(void* str, void* separator) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return nullptr;
        TsString* sep = ts_ensure_flat(separator);
        return s->Split(sep);
    }

    void* ts_string_trim(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->Trim();
    }

    void* ts_string_trimStart(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->TrimStart();
    }

    void* ts_string_trimEnd(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->TrimEnd();
    }

    void* ts_string_substring(void* str, int64_t start, int64_t end) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        // INT64_MIN sentinel means "end not provided" - default to string length
        if (end == INT64_MIN) {
            end = s->Length();
        }
        return s->Substring(start, end);
    }

    void* ts_string_substr(void* str, int64_t start, int64_t length) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        int64_t len = s->Length();
        // Handle negative start (relative to end)
        if (start < 0) start = std::max<int64_t>(len + start, 0);
        if (start >= len) return TsString::Create("");
        // INT64_MIN sentinel means "length not provided" - default to rest of string
        if (length == INT64_MIN) length = len - start;
        if (length <= 0) return TsString::Create("");
        int64_t end = std::min<int64_t>(start + length, len);
        return s->Substring(start, end);
    }

    void* ts_string_slice(void* str, int64_t start, int64_t end) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        // INT64_MIN sentinel means "end not provided" - default to string length
        if (end == INT64_MIN) {
            end = s->Length();
        }
        return s->Slice(start, end);
    }

    void* ts_string_repeat(void* str, int64_t count) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->Repeat(count);
    }

    void* ts_string_padStart(void* str, int64_t targetLength, void* padString) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        TsString* pad = ts_ensure_flat(padString);
        return s->PadStart(targetLength, pad);
    }

    void* ts_string_padEnd(void* str, int64_t targetLength, void* padString) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        TsString* pad = ts_ensure_flat(padString);
        return s->PadEnd(targetLength, pad);
    }

    bool ts_string_startsWith(void* str, void* prefix) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return false;
        TsString* p = ts_ensure_flat(prefix);
        if (!p) return false;
        return s->StartsWith(p);
    }

    bool ts_string_endsWith(void* str, void* suffix) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return false;
        TsString* suf = ts_ensure_flat(suffix);
        if (!suf) return false;
        return s->EndsWith(suf);
    }

    bool ts_string_includes(void* str, void* searchString) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return false;
        TsString* search = ts_ensure_flat(searchString);
        if (!search) return false;
        return s->Includes(search);
    }

    int64_t ts_string_indexOf(void* str, void* searchString) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return -1;
        TsString* search = ts_ensure_flat(searchString);
        if (!search) return -1;
        return s->IndexOf(search);
    }

    int64_t ts_string_lastIndexOf(void* str, void* searchString) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return -1;
        TsString* search = ts_ensure_flat(searchString);
        if (!search) return -1;
        return s->LastIndexOf(search);
    }

    void* ts_string_toLowerCase(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->ToLowerCase();
    }

    void* ts_string_toUpperCase(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->ToUpperCase();
    }

    void* ts_string_normalize(void* str, void* form) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        TsString* formStr = form ? ts_ensure_flat(form) : nullptr;
        return s->Normalize(formStr);
    }

    // Helper: unbox a potential TsValue* to get the raw TsRegExp* pointer.
    // In the JS slow path, arguments may be NaN-boxed TsValue* instead of raw pointers.
    static TsRegExp* unboxRegExp(void* arg) {
        if (!arg) return nullptr;
        // Check if it's already a raw TsRegExp* (magic 0x52454758 = "REGX")
        uint32_t magic = *(uint32_t*)arg;
        if (magic == 0x52454758) return (TsRegExp*)arg;
        // Try to unbox as TsValue*
        void* raw = ts_value_get_object((TsValue*)arg);
        if (raw) {
            magic = *(uint32_t*)raw;
            if (magic == 0x52454758) return (TsRegExp*)raw;
        }
        return (TsRegExp*)arg; // Fallback: assume raw pointer
    }

    void* ts_string_match_regexp(void* str, void* regexp) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return nullptr;
        return s->Match(unboxRegExp(regexp));
    }

    void* ts_string_matchAll_regexp(void* str, void* regexp) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return nullptr;
        return s->MatchAll(unboxRegExp(regexp));
    }

    int64_t ts_string_search_regexp(void* str, void* regexp) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return -1;
        return s->Search(unboxRegExp(regexp));
    }

    // Callback-based regex replacement: call function for each match
    static void* ts_string_replace_callback_regex(TsString* str, TsRegExp* regexp, TsValue* callback) {
        icu::RegexMatcher* matcher = (icu::RegexMatcher*)regexp->GetMatcher();
        if (!matcher) return str;

        icu::UnicodeString input = str->ToUnicodeString();
        matcher->reset(input);

        bool isGlobal = regexp->IsGlobal();
        UErrorCode status = U_ZERO_ERROR;
        icu::UnicodeString result;
        int32_t lastEnd = 0;

        while (matcher->find()) {
            int32_t matchStart = matcher->start(status);
            int32_t matchEnd = matcher->end(status);

            // Append text before this match
            result.append(input, lastEnd, matchStart - lastEnd);

            // Build callback args: (match, g1, g2, ..., offset, originalString)
            int32_t groupCount = matcher->groupCount();
            std::vector<TsValue*> args;
            args.reserve(1 + groupCount + 2);

            // Full match (group 0)
            {
                icu::UnicodeString matchStr = matcher->group(0, status);
                std::string utf8;
                matchStr.toUTF8String(utf8);
                args.push_back(ts_value_make_string(TsString::Create(utf8.c_str())));
            }

            // Capture groups (1..groupCount)
            for (int32_t i = 1; i <= groupCount; i++) {
                int32_t gs = matcher->start(i, status);
                if (gs == -1) {
                    args.push_back(ts_value_make_undefined());
                } else {
                    icu::UnicodeString group = matcher->group(i, status);
                    std::string gUtf8;
                    group.toUTF8String(gUtf8);
                    args.push_back(ts_value_make_string(TsString::Create(gUtf8.c_str())));
                }
            }

            // Offset (index of match)
            args.push_back(ts_value_make_int(matchStart));
            // Original string
            args.push_back(ts_value_make_string(str));

            // Call the callback
            TsValue* callResult = ts_call_variadic_impl(callback, args.data(), (int)args.size());

            // Convert result to string and append
            if (callResult) {
                // Try to extract as string first (most common case)
                void* strPtr = ts_value_get_string(callResult);
                if (strPtr) {
                    icu::UnicodeString replU = ((TsString*)strPtr)->ToUnicodeString();
                    result.append(replU);
                } else {
                    // Fall back to ts_string_from_value for other types (number, bool, etc.)
                    void* converted = ts_string_from_value(callResult);
                    if (converted) {
                        icu::UnicodeString replU = ((TsString*)converted)->ToUnicodeString();
                        result.append(replU);
                    }
                }
            }

            lastEnd = matchEnd;

            // For zero-length matches, advance by 1 to avoid infinite loop
            if (matchStart == matchEnd) {
                if (matchEnd < input.length()) {
                    result.append(input[matchEnd]);
                    lastEnd = matchEnd + 1;
                } else {
                    break;
                }
            }

            if (!isGlobal) break;
        }

        // Append remaining text
        if (lastEnd < input.length()) {
            result.append(input, lastEnd, input.length() - lastEnd);
        }

        std::string utf8Result;
        result.toUTF8String(utf8Result);
        return TsString::Create(utf8Result.c_str());
    }

    // Callback-based string replacement: call function for first match
    static void* ts_string_replace_callback_string(TsString* str, TsString* pattern, TsValue* callback) {
        const char* haystack = str->ToUtf8();
        const char* needle = pattern->ToUtf8();
        if (!haystack || !needle) return str;

        const char* found = strstr(haystack, needle);
        if (!found) return str;

        int64_t offset = found - haystack;
        size_t needleLen = strlen(needle);

        // Call callback with (match, offset, originalString)
        TsValue* callResult = ts_call_3(callback,
            ts_value_make_string(pattern),
            ts_value_make_int(offset),
            ts_value_make_string(str));

        TsString* replStr = callResult ? (TsString*)ts_string_from_value(callResult) : TsString::Create("undefined");
        const char* replUtf8 = replStr->ToUtf8();

        std::string result;
        result.append(haystack, offset);
        result.append(replUtf8);
        result.append(haystack + offset + needleLen);

        return TsString::Create(result.c_str());
    }

    void* ts_string_replace(void* str, void* pattern, void* replacement) {
        TsString* flatStr = ts_ensure_flat(str);
        if (!flatStr) return str;

        // Check if replacement is a callback function
        bool replIsCallback = ts_nanbox_is_callable(replacement);

        // Pattern may be a NaN-boxed TsValue* - try to extract raw object pointer
        if (pattern) {
            void* rawObj = ts_value_get_object((TsValue*)pattern);
            if (rawObj) {
                uint32_t magic = *(uint32_t*)rawObj;
                if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                    if (replIsCallback) {
                        return ts_string_replace_callback_regex(flatStr, (TsRegExp*)rawObj, (TsValue*)replacement);
                    }
                    void* rawRepl = replacement ? ts_value_get_string((TsValue*)replacement) : nullptr;
                    if (!rawRepl) rawRepl = replacement;
                    TsString* flatRepl = ts_ensure_flat(rawRepl);
                    return flatStr->Replace((TsRegExp*)rawObj, flatRepl);
                }
                // Check if it's a TsString or TsConsString
                if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
                    TsString* flatPattern = ts_ensure_flat(rawObj);
                    if (replIsCallback) {
                        return ts_string_replace_callback_string(flatStr, flatPattern, (TsValue*)replacement);
                    }
                    void* rawRepl = replacement ? ts_value_get_string((TsValue*)replacement) : nullptr;
                    if (!rawRepl) rawRepl = replacement;
                    TsString* flatRepl = ts_ensure_flat(rawRepl);
                    return flatStr->Replace(flatPattern, flatRepl);
                }
            }
            // Try extracting as string directly
            void* strPattern = ts_value_get_string((TsValue*)pattern);
            if (strPattern) {
                TsString* flatPattern = ts_ensure_flat(strPattern);
                if (replIsCallback) {
                    return ts_string_replace_callback_string(flatStr, flatPattern, (TsValue*)replacement);
                }
                void* rawRepl = replacement ? ts_value_get_string((TsValue*)replacement) : nullptr;
                if (!rawRepl) rawRepl = replacement;
                TsString* flatRepl = ts_ensure_flat(rawRepl);
                return flatStr->Replace(flatPattern, flatRepl);
            }
        }
        TsString* flatPattern = ts_ensure_flat(pattern);
        TsString* flatRepl = ts_ensure_flat(replacement);
        return flatStr->Replace(flatPattern, flatRepl);
    }

    void* ts_string_replace_regexp(void* str, void* regexp, void* replacement) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        TsString* repl = ts_ensure_flat(replacement);
        return s->Replace(unboxRegExp(regexp), repl);
    }

    void* ts_string_replaceAll(void* str, void* pattern, void* replacement) {
        TsString* flatStr = ts_ensure_flat(str);
        if (!flatStr) return str;

        // Pattern may be a NaN-boxed TsValue* - try to extract raw object pointer
        if (pattern) {
            void* rawObj = ts_value_get_object((TsValue*)pattern);
            if (rawObj) {
                uint32_t magic = *(uint32_t*)rawObj;
                if (magic == 0x52454758) { // TsRegExp::MAGIC ("REGX")
                    void* rawRepl = replacement ? ts_value_get_string((TsValue*)replacement) : nullptr;
                    if (!rawRepl) rawRepl = replacement;
                    TsString* flatRepl = ts_ensure_flat(rawRepl);
                    return flatStr->Replace((TsRegExp*)rawObj, flatRepl);
                }
                if (magic == 0x53545247 || magic == TsConsString::MAGIC) {
                    TsString* flatPattern = ts_ensure_flat(rawObj);
                    void* rawRepl = replacement ? ts_value_get_string((TsValue*)replacement) : nullptr;
                    if (!rawRepl) rawRepl = replacement;
                    TsString* flatRepl = ts_ensure_flat(rawRepl);
                    return flatStr->ReplaceAll(flatPattern, flatRepl);
                }
            }
            void* strPattern = ts_value_get_string((TsValue*)pattern);
            if (strPattern) {
                TsString* flatPattern = ts_ensure_flat(strPattern);
                void* rawRepl = replacement ? ts_value_get_string((TsValue*)replacement) : nullptr;
                if (!rawRepl) rawRepl = replacement;
                TsString* flatRepl = ts_ensure_flat(rawRepl);
                return flatStr->ReplaceAll(flatPattern, flatRepl);
            }
        }
        TsString* flatPattern = ts_ensure_flat(pattern);
        TsString* flatRepl = ts_ensure_flat(replacement);
        return flatStr->ReplaceAll(flatPattern, flatRepl);
    }

    bool ts_string_isWellFormed(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return true;
        return s->IsWellFormed();
    }

    void* ts_string_toWellFormed(void* str) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return str;
        return s->ToWellFormed();
    }

    void* ts_string_split_regexp(void* str, void* regexp) {
        TsString* s = ts_ensure_flat(str);
        if (!s) return nullptr;
        return s->Split(unboxRegExp(regexp));
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
        TsString* flatA = ts_ensure_flat(a);
        TsString* flatB = ts_ensure_flat(b);
        if (!flatA || !flatB) return flatA == flatB;
        return flatA->Equals(flatB);
    }

    void* ts_string_from_value(TsValue* val) {
        if (!val) return TsString::GetInterned("undefined");

        uint64_t nb = nanbox_from_tsvalue_ptr(val);

        if (nanbox_is_undefined(nb)) return TsString::GetInterned("undefined");
        if (nanbox_is_null(nb))      return TsString::GetInterned("null");
        if (nanbox_is_true(nb))      return TsString::GetInterned("true");
        if (nanbox_is_false(nb))     return TsString::GetInterned("false");
        if (nanbox_is_int32(nb))     return TsString::FromInt((int64_t)nanbox_to_int32(nb));
        if (nanbox_is_double(nb))    return TsString::FromDouble(nanbox_to_double(nb));

        if (nanbox_is_ptr(nb)) {
            void* ptr = nanbox_to_ptr(nb);
            if (!ptr) return TsString::GetInterned("null");
            uint32_t magic = *(uint32_t*)ptr;
            if (magic == 0x53545247) return ptr; // TsString
            if (magic == TsConsString::MAGIC) return ((TsConsString*)ptr)->Flatten(); // Flatten cons string
            if (magic == 0x41525259) return TsString::GetInterned("[object Array]");
            if (magic == 0x42494749) { // TsBigInt
                // BigInt toString would need special handling
                return TsString::GetInterned("[object BigInt]");
            }
            return TsString::GetInterned("[object Object]");
        }

        return TsString::GetInterned("unknown");
    }

    // --- URI encoding/decoding functions ---

    static bool isURIUnescaped(unsigned char c) {
        // Unreserved characters per RFC 3986
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '!' ||
               c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
    }

    static bool isURIReserved(unsigned char c) {
        return c == ';' || c == '/' || c == '?' || c == ':' || c == '@' ||
               c == '&' || c == '=' || c == '+' || c == '$' || c == ',' || c == '#';
    }

    static int hexDigit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    void* ts_encode_uri_component(void* str) {
        if (!str) return TsString::GetInterned("undefined");
        TsString* s = ts_ensure_flat(str);
        if (!s) return TsString::GetInterned("");
        const char* utf8 = s->ToUtf8();
        if (!utf8) return TsString::GetInterned("");

        std::string result;
        for (const unsigned char* p = (const unsigned char*)utf8; *p; ++p) {
            if (isURIUnescaped(*p)) {
                result += (char)*p;
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", *p);
                result += hex;
            }
        }
        return TsString::Create(result.c_str());
    }

    void* ts_decode_uri_component(void* str) {
        if (!str) return TsString::GetInterned("undefined");
        TsString* s = ts_ensure_flat(str);
        if (!s) return TsString::GetInterned("");
        const char* utf8 = s->ToUtf8();
        if (!utf8) return TsString::GetInterned("");

        std::string result;
        for (const char* p = utf8; *p; ++p) {
            if (*p == '%' && p[1] && p[2]) {
                int hi = hexDigit(p[1]);
                int lo = hexDigit(p[2]);
                if (hi >= 0 && lo >= 0) {
                    result += (char)((hi << 4) | lo);
                    p += 2;
                } else {
                    result += *p;
                }
            } else {
                result += *p;
            }
        }
        return TsString::Create(result.c_str());
    }

    void* ts_encode_uri(void* str) {
        if (!str) return TsString::GetInterned("undefined");
        TsString* s = ts_ensure_flat(str);
        if (!s) return TsString::GetInterned("");
        const char* utf8 = s->ToUtf8();
        if (!utf8) return TsString::GetInterned("");

        std::string result;
        for (const unsigned char* p = (const unsigned char*)utf8; *p; ++p) {
            if (isURIUnescaped(*p) || isURIReserved(*p)) {
                result += (char)*p;
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", *p);
                result += hex;
            }
        }
        return TsString::Create(result.c_str());
    }

    void* ts_decode_uri(void* str) {
        if (!str) return TsString::GetInterned("undefined");
        TsString* s = ts_ensure_flat(str);
        if (!s) return TsString::GetInterned("");
        const char* utf8 = s->ToUtf8();
        if (!utf8) return TsString::GetInterned("");

        std::string result;
        for (const char* p = utf8; *p; ++p) {
            if (*p == '%' && p[1] && p[2]) {
                int hi = hexDigit(p[1]);
                int lo = hexDigit(p[2]);
                if (hi >= 0 && lo >= 0) {
                    unsigned char decoded = (unsigned char)((hi << 4) | lo);
                    // Don't decode reserved characters
                    if (isURIReserved(decoded)) {
                        result += *p;
                    } else {
                        result += (char)decoded;
                        p += 2;
                    }
                } else {
                    result += *p;
                }
            } else {
                result += *p;
            }
        }
        return TsString::Create(result.c_str());
    }

    void* ts_escape(void* str) {
        if (!str) return TsString::GetInterned("undefined");
        TsString* s = (TsString*)str;
        const char* utf8 = s->ToUtf8();
        if (!utf8) return TsString::GetInterned("");

        std::string result;
        for (const unsigned char* p = (const unsigned char*)utf8; *p; ++p) {
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                (*p >= '0' && *p <= '9') || *p == '@' || *p == '*' ||
                *p == '_' || *p == '+' || *p == '-' || *p == '.' || *p == '/') {
                result += (char)*p;
            } else if (*p < 256) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", *p);
                result += hex;
            }
        }
        return TsString::Create(result.c_str());
    }

    void* ts_unescape(void* str) {
        if (!str) return TsString::GetInterned("undefined");
        TsString* s = (TsString*)str;
        const char* utf8 = s->ToUtf8();
        if (!utf8) return TsString::GetInterned("");

        std::string result;
        for (const char* p = utf8; *p; ++p) {
            if (*p == '%' && p[1] && p[2]) {
                // Check for %uXXXX (unicode escape)
                if (p[1] == 'u' && p[2] && p[3] && p[4] && p[5]) {
                    int d1 = hexDigit(p[2]);
                    int d2 = hexDigit(p[3]);
                    int d3 = hexDigit(p[4]);
                    int d4 = hexDigit(p[5]);
                    if (d1 >= 0 && d2 >= 0 && d3 >= 0 && d4 >= 0) {
                        uint32_t codepoint = (d1 << 12) | (d2 << 8) | (d3 << 4) | d4;
                        // Encode as UTF-8
                        if (codepoint < 0x80) {
                            result += (char)codepoint;
                        } else if (codepoint < 0x800) {
                            result += (char)(0xC0 | (codepoint >> 6));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            result += (char)(0xE0 | (codepoint >> 12));
                            result += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            result += (char)(0x80 | (codepoint & 0x3F));
                        }
                        p += 5;
                        continue;
                    }
                }
                int hi = hexDigit(p[1]);
                int lo = hexDigit(p[2]);
                if (hi >= 0 && lo >= 0) {
                    result += (char)((hi << 4) | lo);
                    p += 2;
                } else {
                    result += *p;
                }
            } else {
                result += *p;
            }
        }
        return TsString::Create(result.c_str());
    }
}
