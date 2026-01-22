#include "TsRuntime.h"
#include "TsString.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "TsArray.h"
#include "TsMap.h"
#include "TsSet.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <map>
#include <string>
#include <cstdlib>
#include <windows.h>

static std::map<std::string, std::chrono::steady_clock::time_point> consoleTimers;
static std::map<std::string, int64_t> consoleCounters;
static int consoleGroupDepth = 0;

// Helper to print indentation based on console group depth
static void printConsoleIndent(FILE* stream = stdout) {
    for (int i = 0; i < consoleGroupDepth; i++) {
        std::fprintf(stream, "  ");
    }
}

extern "C" {

void ts_console_error(TsString* str) {
    printConsoleIndent(stderr);
    if (str) {
        std::fprintf(stderr, "%s\n", str->ToUtf8());
    } else {
        std::fprintf(stderr, "undefined\n");
    }
    std::fflush(stderr);
}

void ts_console_error_int(int64_t val) {
    printConsoleIndent(stderr);
    std::fprintf(stderr, "%lld\n", val);
    std::fflush(stderr);
}

void ts_console_error_double(double val) {
    printConsoleIndent(stderr);
    std::fprintf(stderr, "%f\n", val);
    std::fflush(stderr);
}

void ts_console_error_bool(bool val) {
    printConsoleIndent(stderr);
    std::fprintf(stderr, "%s\n", val ? "true" : "false");
    std::fflush(stderr);
}

// Forward declaration - implemented after ts_console_log_bool
static void ts_console_print_value_to_stream(TsValue* val, FILE* stream);

void ts_console_error_value(TsValue* val) {
    printConsoleIndent(stderr);
    ts_console_print_value_to_stream(val, stderr);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void ts_console_time(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    consoleTimers[key] = std::chrono::steady_clock::now();
}

void ts_console_time_end(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    auto it = consoleTimers.find(key);
    if (it != consoleTimers.end()) {
        auto end = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - it->second).count();
        std::printf("%s: %lldms\n", key.c_str(), (long long)diff);
        consoleTimers.erase(it);
    }
}

void ts_console_trace() {
    std::printf("Trace: (stack trace not yet implemented)\n");
}

void ts_console_time_log(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    auto it = consoleTimers.find(key);
    if (it != consoleTimers.end()) {
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        std::printf("%s: %lldms\n", key.c_str(), (long long)diff);
    } else {
        std::printf("Timer '%s' does not exist\n", key.c_str());
    }
    std::fflush(stdout);
}

void ts_console_dir(TsValue* val) {
    // Forward declaration - use ts_console_print_value_to_stream
    ts_console_print_value_to_stream(val, stdout);
    std::printf("\n");
    std::fflush(stdout);
}

void ts_console_count(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    consoleCounters[key]++;
    std::printf("%s: %lld\n", key.c_str(), (long long)consoleCounters[key]);
    std::fflush(stdout);
}

void ts_console_count_reset(TsString* label) {
    std::string key = label ? label->ToUtf8() : "default";
    consoleCounters[key] = 0;
}

void ts_console_group(TsString* label) {
    // Print indent based on current depth
    for (int i = 0; i < consoleGroupDepth; i++) {
        std::printf("  ");
    }
    if (label) {
        std::printf("%s\n", label->ToUtf8());
    }
    consoleGroupDepth++;
    std::fflush(stdout);
}

void ts_console_group_collapsed(TsString* label) {
    // In a terminal, collapsed behaves the same as group
    // (collapsing is a browser DevTools feature)
    ts_console_group(label);
}

void ts_console_group_end() {
    if (consoleGroupDepth > 0) {
        consoleGroupDepth--;
    }
}

void ts_console_clear() {
    // Clear terminal screen using ANSI escape codes
    // Works on Windows 10+ and Unix terminals
    std::printf("\033[2J\033[H");
    std::fflush(stdout);
}

void ts_console_table(TsValue* data, TsValue* properties) {
    // console.table displays data in tabular format
    // For arrays of objects, show columns for each property
    // For arrays of primitives, show index and value columns
    // For objects, show key-value pairs

    if (!data) {
        std::printf("undefined\n");
        std::fflush(stdout);
        return;
    }

    printConsoleIndent();

    // Check if it's an array
    TsArray* arr = nullptr;
    TsMap* obj = nullptr;

    if (data->type == ValueType::ARRAY_PTR) {
        arr = (TsArray*)data->ptr_val;
    } else if (data->type == ValueType::OBJECT_PTR) {
        void* ptr = data->ptr_val;
        // Check for TsArray at offset 8 (after vtable - TsArray doesn't have virtual methods)
        uint32_t magic8 = *((uint32_t*)((char*)ptr + 8));
        // Check for TsMap at offset 16 (after vtable + impl pointer)
        uint32_t magic16 = *((uint32_t*)((char*)ptr + 16));
        if (magic8 == TsArray::MAGIC) {
            arr = (TsArray*)ptr;
        } else if (magic16 == TsMap::MAGIC) {
            obj = (TsMap*)ptr;
        }
    }

    if (arr) {
        // Print array in tabular format
        int64_t len = arr->Length();
        std::printf("| (index) | Value |\n");
        std::printf("|---------|-------|\n");
        for (int64_t i = 0; i < len; i++) {
            TsValue* elem = arr->GetElementBoxed(i);
            std::printf("| %lld | ", (long long)i);
            ts_console_print_value_to_stream(elem, stdout);
            std::printf(" |\n");
        }
    } else if (obj) {
        // Print object in tabular format
        std::printf("| (key) | Value |\n");
        std::printf("|-------|-------|\n");
        TsArray* keys = (TsArray*)obj->GetKeys();
        if (keys) {
            int64_t len = keys->Length();
            for (int64_t i = 0; i < len; i++) {
                TsValue* keyVal = keys->GetElementBoxed(i);
                if (keyVal) {
                    // GetElementBoxed wraps pointer in OBJECT_PTR
                    // The actual key TsValue is inside ptr_val
                    TsValue* actualKey = nullptr;
                    TsString* keyStr = nullptr;

                    if (keyVal->type == ValueType::OBJECT_PTR && keyVal->ptr_val) {
                        actualKey = (TsValue*)keyVal->ptr_val;
                        if (actualKey->type == ValueType::STRING_PTR) {
                            keyStr = (TsString*)actualKey->ptr_val;
                        }
                    } else if (keyVal->type == ValueType::STRING_PTR) {
                        keyStr = (TsString*)keyVal->ptr_val;
                        actualKey = keyVal;
                    }

                    if (keyStr && actualKey) {
                        TsValue val = obj->Get(*actualKey);
                        std::printf("| %s | ", keyStr->ToUtf8());
                        ts_console_print_value_to_stream(&val, stdout);
                        std::printf(" |\n");
                    }
                }
            }
        }
    } else {
        // Just print the value
        ts_console_print_value_to_stream(data, stdout);
        std::printf("\n");
    }
    std::fflush(stdout);
}

int32_t ts_double_to_int32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    double i = std::trunc(std::fmod(d, 4294967296.0));
    if (i < 0) i += 4294967296.0;
    if (i >= 2147483648.0) i -= 4294967296.0;
    return (int32_t)i;
}

uint32_t ts_double_to_uint32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    double i = std::trunc(std::fmod(d, 4294967296.0));
    if (i < 0) i += 4294967296.0;
    return (uint32_t)i;
}

void ts_console_log(TsString* str) {
    printConsoleIndent();
    if (str) {
        std::printf("%s\n", str->ToUtf8());
    } else {
        std::printf("undefined\n");
    }
    std::fflush(stdout);
}

void ts_console_log_int(int64_t val) {
    printConsoleIndent();
    std::printf("%lld\n", val);
    std::fflush(stdout);
}

void ts_console_log_double(double val) {
    printConsoleIndent();
    std::printf("%f\n", val);
    std::fflush(stdout);
}

void ts_console_log_bool(bool val) {
    printConsoleIndent();
    std::printf("%s\n", val ? "true" : "false");
    std::fflush(stdout);
}

// Internal helper to print value to any stream (stdout or stderr)
static void ts_console_print_value_to_stream(TsValue* val, FILE* stream) {
    if (!val) {
        std::fprintf(stream, "undefined");
        return;
    }

    uint32_t magic = *(uint32_t*)val;
    if (magic == 0x53545247) {
        std::fprintf(stream, "%s", ((TsString*)val)->ToUtf8());
        return;
    }

    if (magic == 0x4D415053) { // MAPS
        std::fprintf(stream, "Map(%lld)", ((TsMap*)val)->Size());
        return;
    }

    if (magic == 0x53455453) { // SETS
        std::fprintf(stream, "Set(%lld)", ((TsSet*)val)->Size());
        return;
    }

    if (magic == 0x41525259) {
        TsArray* arr = (TsArray*)val;
        int64_t len = arr->Length();
        std::fprintf(stream, "[ ");
        for (int64_t i = 0; i < len; i++) {
            int64_t rawElem = arr->Get(i);
            if (i > 0) std::fprintf(stream, ", ");

            // Check if this looks like a raw integer (small value) or a pointer
            if (rawElem == 0) {
                std::fprintf(stream, "undefined");
            } else if (rawElem > 0 && rawElem <= 0xFFFFFFFF) {
                // Small positive value - treat as integer
                std::fprintf(stream, "%lld", rawElem);
            } else {
                // Large value or negative - likely a pointer, try to access
                void* elem = (void*)rawElem;
                __try {
                    TsValue* elemVal = (TsValue*)elem;
                    if (elemVal->type == ValueType::STRING_PTR && elemVal->ptr_val) {
                        std::fprintf(stream, "'%s'", ((TsString*)elemVal->ptr_val)->ToUtf8());
                    } else if (elemVal->type == ValueType::NUMBER_INT) {
                        std::fprintf(stream, "%lld", elemVal->i_val);
                    } else if (elemVal->type == ValueType::NUMBER_DBL) {
                        std::fprintf(stream, "%f", elemVal->d_val);
                    } else {
                        uint32_t elemMagic = *(uint32_t*)elem;
                        if (elemMagic == 0x53545247) {
                            std::fprintf(stream, "'%s'", ((TsString*)elem)->ToUtf8());
                        } else {
                            std::fprintf(stream, "[object Object]");
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    // Not a valid pointer, treat as integer
                    std::fprintf(stream, "%lld", rawElem);
                }
            }
        }
        std::fprintf(stream, " ]");
        return;
    }

    switch (val->type) {
        case ValueType::UNDEFINED: std::fprintf(stream, "undefined"); break;
        case ValueType::NUMBER_INT: std::fprintf(stream, "%lld", val->i_val); break;
        case ValueType::NUMBER_DBL: std::fprintf(stream, "%f", val->d_val); break;
        case ValueType::BOOLEAN: std::fprintf(stream, "%s", val->b_val ? "true" : "false"); break;
        case ValueType::STRING_PTR: std::fprintf(stream, "%s", ((TsString*)val->ptr_val)->ToUtf8()); break;
        case ValueType::OBJECT_PTR: {
            if (!val->ptr_val) {
                std::fprintf(stream, "null");
            } else {
                // Check if ptr_val points to a TsValue (nested boxing)
                TsValue* inner = (TsValue*)val->ptr_val;
                uint8_t firstByte = *(uint8_t*)inner;
                if (firstByte <= 10) {
                    // Looks like a TsValue - recurse
                    ts_console_print_value_to_stream(inner, stream);
                } else {
                    uint32_t objMagic = *(uint32_t*)val->ptr_val;
                    if (objMagic == 0x4D415053) { // MAPS
                        std::fprintf(stream, "Map(%lld)", ((TsMap*)val->ptr_val)->Size());
                    } else if (objMagic == 0x53455453) { // SETS
                        std::fprintf(stream, "Set(%lld)", ((TsSet*)val->ptr_val)->Size());
                    } else if (objMagic == 0x53545247) { // STRG
                        std::fprintf(stream, "%s", ((TsString*)val->ptr_val)->ToUtf8());
                    } else {
                        std::fprintf(stream, "[object Object]");
                    }
                }
            }
            break;
        }
        case ValueType::ARRAY_PTR: {
            TsArray* arr = (TsArray*)val->ptr_val;
            int64_t len = arr->Length();
            std::fprintf(stream, "[ ");
            for (int64_t i = 0; i < len; i++) {
                int64_t rawElem = arr->Get(i);
                if (i > 0) std::fprintf(stream, ", ");

                // Check if this looks like a raw integer (small value) or a pointer
                // Pointers on Windows x64 are typically > 0x10000 and have high bits set
                if (rawElem == 0) {
                    std::fprintf(stream, "undefined");
                } else if (rawElem > 0 && rawElem <= 0xFFFFFFFF) {
                    // Small positive value - treat as integer
                    std::fprintf(stream, "%lld", rawElem);
                } else if (rawElem < 0) {
                    // Could be negative number or pointer
                    // Try to detect if it's a valid pointer
                    void* elem = (void*)rawElem;
                    __try {
                        TsValue* elemVal = (TsValue*)elem;
                        if (elemVal->type == ValueType::STRING_PTR && elemVal->ptr_val) {
                            std::fprintf(stream, "'%s'", ((TsString*)elemVal->ptr_val)->ToUtf8());
                        } else if (elemVal->type == ValueType::NUMBER_INT) {
                            std::fprintf(stream, "%lld", elemVal->i_val);
                        } else if (elemVal->type == ValueType::NUMBER_DBL) {
                            std::fprintf(stream, "%f", elemVal->d_val);
                        } else {
                            uint32_t elemMagic = *(uint32_t*)elem;
                            if (elemMagic == 0x53545247) {
                                std::fprintf(stream, "'%s'", ((TsString*)elem)->ToUtf8());
                            } else {
                                std::fprintf(stream, "[object Object]");
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {
                        // Not a valid pointer, treat as negative integer
                        std::fprintf(stream, "%lld", rawElem);
                    }
                } else {
                    // Large positive value - likely a pointer
                    void* elem = (void*)rawElem;
                    TsValue* elemVal = (TsValue*)elem;
                    if (elemVal->type == ValueType::STRING_PTR && elemVal->ptr_val) {
                        std::fprintf(stream, "'%s'", ((TsString*)elemVal->ptr_val)->ToUtf8());
                    } else if (elemVal->type == ValueType::NUMBER_INT) {
                        std::fprintf(stream, "%lld", elemVal->i_val);
                    } else if (elemVal->type == ValueType::NUMBER_DBL) {
                        std::fprintf(stream, "%f", elemVal->d_val);
                    } else {
                        uint32_t elemMagic = *(uint32_t*)elem;
                        if (elemMagic == 0x53545247) {
                            std::fprintf(stream, "'%s'", ((TsString*)elem)->ToUtf8());
                        } else {
                            std::fprintf(stream, "[object Object]");
                        }
                    }
                }
            }
            std::fprintf(stream, " ]");
            break;
        }
        case ValueType::PROMISE_PTR: std::fprintf(stream, "[object Promise]"); break;
        case ValueType::BIGINT_PTR: std::fprintf(stream, "%sn", ((TsBigInt*)val->ptr_val)->ToString()); break;
        case ValueType::SYMBOL_PTR: {
            TsSymbol* sym = (TsSymbol*)val->ptr_val;
            if (sym->description) {
                std::fprintf(stream, "Symbol(%s)", sym->description->ToUtf8());
            } else {
                std::fprintf(stream, "Symbol()");
            }
            break;
        }
    }
}

extern "C" void ts_console_log_value_no_newline(TsValue* val) {
    printConsoleIndent();
    ts_console_print_value_to_stream(val, stdout);
}

extern "C" void ts_console_log_value(TsValue* val) {
    printConsoleIndent();
    ts_console_print_value_to_stream(val, stdout);
    std::printf("\n");
    std::fflush(stdout);
}


TsString* ts_typeof(void* val) {
    if (!val) return TsString::Create("undefined");
    
    uintptr_t ptr = (uintptr_t)val;
    // Check for bitcasted double (high bits set) - this is NOT a valid pointer
    if (ptr > 0x00007FFFFFFFFFFF) {
        return TsString::Create("number");
    }
    
    // First check for magic numbers - these are definitive
    uint32_t magic = *(uint32_t*)val;
    if (magic == 0x53545247) { // TsString
        return TsString::Create("string");
    }
    if (magic == 0x42494749) { // TsBigInt
        return TsString::Create("bigint");
    }
    if (magic == 0x53594D42) { // TsSymbol
        return TsString::Create("symbol");
    }
    if (magic == 0x41525259) { // TsArray
        return TsString::Create("object");
    }
    if (magic == 0x4D415053) { // TsMap
        return TsString::Create("object");
    }
    if (magic == 0x46554E43) { // TsFunction
        return TsString::Create("function");
    }
    
    // Now check for boxed TsValue* 
    // A TsValue has type byte at offset 0, and the valid range is 0-10 (includes FUNCTION_PTR)
    // Magic numbers have much larger first bytes (0x41, 0x42, 0x46, 0x4D, 0x53)
    uint8_t firstByte = *(uint8_t*)val;
    if (firstByte <= 10) {
        TsValue* tv = (TsValue*)val;
        switch (tv->type) {
            case ValueType::UNDEFINED:
                return TsString::Create("undefined");
            case ValueType::NUMBER_INT:
            case ValueType::NUMBER_DBL:
                return TsString::Create("number");
            case ValueType::BOOLEAN:
                return TsString::Create("boolean");
            case ValueType::STRING_PTR:
                return TsString::Create("string");
            case ValueType::FUNCTION_PTR:
                return TsString::Create("function");
            case ValueType::OBJECT_PTR:
                if (!tv->ptr_val) return TsString::Create("object"); // null
                // Recursively check the inner pointer
                return ts_typeof(tv->ptr_val);
            case ValueType::ARRAY_PTR:
                return TsString::Create("object");
            case ValueType::PROMISE_PTR:
                return TsString::Create("object");
            case ValueType::BIGINT_PTR:
                return TsString::Create("bigint");
            case ValueType::SYMBOL_PTR:
                return TsString::Create("symbol");
        }
    }
    
    return TsString::Create("object");
}

bool ts_value_is_nullish(TsValue* v) {
    if (!v) return true;
    if (v->type == ValueType::UNDEFINED) return true;
    if (v->type == ValueType::OBJECT_PTR && v->ptr_val == nullptr) return true;
    return false;
}

bool ts_instanceof(void* obj, void* targetVTable) {
    if (!obj || !targetVTable) return false;
    
    uintptr_t ptr = (uintptr_t)obj;
    if (ptr > 0x00007FFFFFFFFFFF) return false; // Bitcasted double
    
    // Check for TsString magic number
    uint32_t magic = *(uint32_t*)obj;
    if (magic == 0x53545247) return false; // Strings are not instances of classes (for now)
    
    // Get vptr from object (first field)
    void** vptr = *(void***)obj;
    if (!vptr) return false;
    
    // Traverse parent pointers in VTable
    void** currentVTable = vptr;
    while (currentVTable) {
        if (currentVTable == targetVTable) return true;
        currentVTable = (void**)currentVTable[0]; // Parent VTable is at index 0
    }
    
    return false;
}

int64_t ts_value_get_int(TsValue* v) {
    if (!v) return 0;
    if (v->type == ValueType::NUMBER_INT) return v->i_val;
    if (v->type == ValueType::NUMBER_DBL) return (int64_t)v->d_val;
    return 0;
}

double ts_value_get_double(TsValue* v) {
    if (!v) return 0.0;
    
    // Check for raw TsString* (magic at offset 0)
    uint32_t magic = *(uint32_t*)v;
    if (magic == 0x53545247) { // TsString::MAGIC
        TsString* str = (TsString*)v;
        try {
            return std::stod(str->ToUtf8());
        } catch (...) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    if (v->type == ValueType::NUMBER_DBL) return v->d_val;
    if (v->type == ValueType::NUMBER_INT) return (double)v->i_val;
    if (v->type == ValueType::BOOLEAN) return v->b_val ? 1.0 : 0.0;
    if (v->type == ValueType::STRING_PTR) {
        TsString* str = (TsString*)v->ptr_val;
        try {
            return std::stod(str->ToUtf8());
        } catch (...) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }
    return 0.0;
}

int64_t ts_parseInt(void* value) {
    if (!value) return 0;

    // Raw TsString*
    uint32_t magic = *(uint32_t*)value;
    if (magic == 0x53545247) { // TsString::MAGIC
        const char* s = ((TsString*)value)->ToUtf8();
        if (!s) return 0;
        char* end = nullptr;
        long long v = std::strtoll(s, &end, 10);
        (void)end;
        return (int64_t)v;
    }

    // Boxed TsValue*
    uint8_t firstByte = *(uint8_t*)value;
    if (firstByte <= 10) {
        TsValue* v = (TsValue*)value;
        if (v->type == ValueType::NUMBER_INT) return v->i_val;
        if (v->type == ValueType::NUMBER_DBL) return (int64_t)v->d_val;
        if (v->type == ValueType::BOOLEAN) return v->b_val ? 1 : 0;
        if (v->type == ValueType::STRING_PTR) {
            TsString* str = (TsString*)v->ptr_val;
            const char* s = str ? str->ToUtf8() : nullptr;
            if (!s) return 0;
            char* end = nullptr;
            long long n = std::strtoll(s, &end, 10);
            (void)end;
            return (int64_t)n;
        }
        return 0;
    }

    return 0;
}

bool ts_value_to_bool(TsValue* v) {
    if (!v) return false;

    // Check type field first - TsValue types are 0-10
    // If the first byte is > 10, this could be a raw object pointer (vtable pointer)
    uint8_t typeField = *(uint8_t*)v;
    uint8_t byte1 = *((uint8_t*)v + 1);
    uint8_t byte2 = *((uint8_t*)v + 2);
    uint8_t byte3 = *((uint8_t*)v + 3);

    // A TsValue has type <= 10 AND bytes 1-3 are zero (padding)
    // A vtable pointer (raw object) has first byte that may be anything, but bytes 1-3 are part of address
    bool isProbablyTsValue = (typeField <= 10 && byte1 == 0 && byte2 == 0 && byte3 == 0);

    if (!isProbablyTsValue) {
        // This is likely a raw object pointer - check for known magic values
        uint32_t magic = *(uint32_t*)v;
        if (magic == 0x53545247) { // TsString::MAGIC "STRG"
            TsString* str = (TsString*)v;
            return str->Length() > 0;  // Empty string is falsy
        }
        if (magic == 0x41525259) { // TsArray::MAGIC "ARRY"
            return true;  // Arrays are always truthy
        }

        // Check TsMap magic at offset 16 (after vtable ptr + explicit vtable)
        uint32_t magic16 = *(uint32_t*)((char*)v + 16);
        uint32_t magic20 = *(uint32_t*)((char*)v + 20);
        uint32_t magic24 = *(uint32_t*)((char*)v + 24);
        if (magic16 == 0x4D415053 || magic20 == 0x4D415053 || magic24 == 0x4D415053) { // TsMap::MAGIC "MAPS"
            return true;  // Objects are always truthy
        }

        // Unknown pointer type - treat as truthy (non-null pointer)
        return true;
    }

    switch (v->type) {
        case ValueType::UNDEFINED: return false;
        case ValueType::NUMBER_INT: return v->i_val != 0;
        case ValueType::NUMBER_DBL: {
            // NaN is falsy, 0.0 is falsy
            double d = v->d_val;
            return d != 0.0 && !std::isnan(d);
        }
        case ValueType::BOOLEAN: return v->b_val;
        case ValueType::STRING_PTR: {
            if (!v->ptr_val) return false;
            TsString* str = (TsString*)v->ptr_val;
            return str->Length() > 0;  // Empty string is falsy
        }
        case ValueType::OBJECT_PTR:
        case ValueType::PROMISE_PTR:
        case ValueType::ARRAY_PTR:
        case ValueType::BIGINT_PTR:
        case ValueType::SYMBOL_PTR:
        case ValueType::FUNCTION_PTR:
            return v->ptr_val != nullptr;
        default: return false;
    }
}

TsValue* ts_value_strict_eq(TsValue* lhs, TsValue* rhs);

TsValue* ts_value_strict_eq_wrapper(TsValue* lhs, TsValue* rhs) {
    return ts_value_strict_eq(lhs, rhs);
}

}
