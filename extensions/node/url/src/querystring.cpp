#include "TsQueryString.h"
#include "TsMap.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsNanBox.h"
#include "GC.h"
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

// Forward declare ts_string_from_value
extern "C" void* ts_string_from_value(TsValue* val);

// Helper to get string from TsString or return default
static const char* getStringOrDefault(void* str, const char* defaultVal) {
    if (!str) return defaultVal;
    TsString* tsStr = (TsString*)str;
    return tsStr->ToUtf8();
}

// Helper to check if character needs percent encoding
static bool needsEscape(char c) {
    // RFC 3986 unreserved characters: A-Z a-z 0-9 - _ . ~
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
        return false;
    }
    return true;
}

// Helper to convert hex char to value
static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Helper to create a TsValue from TsString*
static TsValue makeStringValue(TsString* str) {
    TsValue v;
    v.type = ValueType::STRING_PTR;
    v.ptr_val = str;
    return v;
}

// Helper to create a TsValue from TsArray*
static TsValue makeArrayValue(TsArray* arr) {
    TsValue v;
    v.type = ValueType::ARRAY_PTR;
    v.ptr_val = arr;
    return v;
}

extern "C" {

void* ts_querystring_escape(void* str) {
    if (!str) return TsString::Create("");

    TsString* input = (TsString*)str;
    const char* s = input->ToUtf8();

    std::ostringstream escaped;
    escaped << std::hex << std::uppercase;

    while (*s) {
        unsigned char c = *s;
        if (needsEscape(c)) {
            escaped << '%' << std::setw(2) << std::setfill('0') << (int)c;
        } else {
            escaped << c;
        }
        s++;
    }

    return TsString::Create(escaped.str().c_str());
}

void* ts_querystring_unescape(void* str) {
    if (!str) return TsString::Create("");

    TsString* input = (TsString*)str;
    const char* s = input->ToUtf8();

    std::string result;
    result.reserve(strlen(s));

    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            int high = hexValue(s[1]);
            int low = hexValue(s[2]);
            if (high >= 0 && low >= 0) {
                result += (char)((high << 4) | low);
                s += 3;
                continue;
            }
        }
        if (*s == '+') {
            result += ' ';  // + decodes to space in query strings
        } else {
            result += *s;
        }
        s++;
    }

    return TsString::Create(result.c_str());
}

void* ts_querystring_parse(void* str, void* sep, void* eq) {
    // Get separator and equals chars (defaults: & and =)
    const char* sepStr = getStringOrDefault(sep, "&");
    const char* eqStr = getStringOrDefault(eq, "=");

    // Create result object (TsMap)
    TsMap* result = TsMap::Create();

    if (!str) {
        return ts_value_make_object(result);
    }

    TsString* input = (TsString*)str;
    const char* query = input->ToUtf8();

    // Split by separator
    std::string queryStr(query);
    std::string sepString(sepStr);
    std::string eqString(eqStr);

    size_t pos = 0;
    while (pos < queryStr.length()) {
        // Find next separator
        size_t sepPos = queryStr.find(sepString, pos);
        if (sepPos == std::string::npos) sepPos = queryStr.length();

        // Extract key=value pair
        std::string pair = queryStr.substr(pos, sepPos - pos);
        pos = sepPos + sepString.length();

        if (pair.empty()) continue;

        // Split by equals
        size_t eqPos = pair.find(eqString);
        std::string key, value;

        if (eqPos == std::string::npos) {
            key = pair;
            value = "";
        } else {
            key = pair.substr(0, eqPos);
            value = pair.substr(eqPos + eqString.length());
        }

        // Unescape key and value
        TsString* unescapedKey = (TsString*)ts_querystring_unescape(TsString::Create(key.c_str()));
        TsString* unescapedValue = (TsString*)ts_querystring_unescape(TsString::Create(value.c_str()));

        // Create TsValue key for map lookup
        TsValue keyTsValue = makeStringValue(unescapedKey);

        // Check if key already exists
        TsValue existingVal = result->Get(keyTsValue);

        if (existingVal.type != ValueType::UNDEFINED) {
            // Key exists - convert to array if needed
            if (existingVal.type == ValueType::ARRAY_PTR) {
                TsArray* arr = (TsArray*)existingVal.ptr_val;
                // Already an array, push new value
                TsValue* newVal = (TsValue*)ts_alloc(sizeof(TsValue));
                *newVal = makeStringValue(unescapedValue);
                arr->Push((int64_t)newVal);
            } else {
                // Existing value is not an array - create array with existing + new
                TsArray* newArr = TsArray::Create();
                TsValue* existingCopy = (TsValue*)ts_alloc(sizeof(TsValue));
                *existingCopy = existingVal;
                newArr->Push((int64_t)existingCopy);
                TsValue* newVal = (TsValue*)ts_alloc(sizeof(TsValue));
                *newVal = makeStringValue(unescapedValue);
                newArr->Push((int64_t)newVal);
                result->Set(keyTsValue, makeArrayValue(newArr));
            }
        } else {
            // New key - just set the value
            result->Set(keyTsValue, makeStringValue(unescapedValue));
        }
    }

    return ts_value_make_object(result);
}

void* ts_querystring_stringify(void* obj, void* sep, void* eq) {
    // Get separator and equals chars (defaults: & and =)
    const char* sepStr = getStringOrDefault(sep, "&");
    const char* eqStr = getStringOrDefault(eq, "=");

    if (!obj) return TsString::Create("");

    // Unbox if needed
    void* rawPtr = ts_value_get_object((TsValue*)obj);
    if (!rawPtr) rawPtr = obj;

    TsMap* map = dynamic_cast<TsMap*>((TsObject*)rawPtr);
    if (!map) return TsString::Create("");

    std::ostringstream result;
    bool first = true;

    // Get keys - returns TsArray* containing TsValue* elements
    TsArray* keys = (TsArray*)map->GetKeys();
    int64_t keyCount = keys->Length();

    for (int64_t i = 0; i < keyCount; i++) {
        TsValue* keyRaw = (TsValue*)keys->Get(i);
        TsValue keyDec = nanbox_to_tagged(keyRaw);
        if (keyDec.type == ValueType::UNDEFINED) continue;

        TsString* keyStr = nullptr;
        if (keyDec.type == ValueType::STRING_PTR) {
            keyStr = (TsString*)keyDec.ptr_val;
        } else {
            continue;  // Skip non-string keys
        }

        TsValue valueVal = map->Get(keyDec);
        if (valueVal.type == ValueType::UNDEFINED) continue;

        // Escape the key
        TsString* escapedKey = (TsString*)ts_querystring_escape(keyStr);
        const char* keyUtf8 = escapedKey->ToUtf8();

        // Handle arrays
        if (valueVal.type == ValueType::ARRAY_PTR) {
            TsArray* arr = (TsArray*)valueVal.ptr_val;
            // Output each array element with the same key
            int64_t arrLen = arr->Length();
            for (int64_t j = 0; j < arrLen; j++) {
                TsValue* elemRaw = (TsValue*)arr->Get(j);
                if (!elemRaw) continue;
                TsValue elemDec = nanbox_to_tagged(elemRaw);

                if (!first) result << sepStr;
                first = false;

                result << keyUtf8 << eqStr;

                if (elemDec.type == ValueType::STRING_PTR) {
                    TsString* escapedValue = (TsString*)ts_querystring_escape((TsString*)elemDec.ptr_val);
                    result << escapedValue->ToUtf8();
                } else {
                    // Convert to string
                    TsString* strVal = (TsString*)ts_string_from_value(elemRaw);
                    TsString* escapedValue = (TsString*)ts_querystring_escape(strVal);
                    result << escapedValue->ToUtf8();
                }
            }
            continue;
        }

        // Single value
        if (!first) result << sepStr;
        first = false;

        result << keyUtf8 << eqStr;

        if (valueVal.type == ValueType::STRING_PTR) {
            TsString* escapedValue = (TsString*)ts_querystring_escape((TsString*)valueVal.ptr_val);
            result << escapedValue->ToUtf8();
        } else {
            // Convert to string - need to allocate TsValue on heap for ts_string_from_value
            TsValue* heapVal = (TsValue*)ts_alloc(sizeof(TsValue));
            *heapVal = valueVal;
            TsString* strVal = (TsString*)ts_string_from_value(heapVal);
            TsString* escapedValue = (TsString*)ts_querystring_escape(strVal);
            result << escapedValue->ToUtf8();
        }
    }

    return TsString::Create(result.str().c_str());
}

} // extern "C"
