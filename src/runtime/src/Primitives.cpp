#include "TsRuntime.h"
#include "TsString.h"
#include "TsBigInt.h"
#include "TsSymbol.h"
#include "TsArray.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <map>
#include <string>

static std::map<std::string, std::chrono::steady_clock::time_point> consoleTimers;

extern "C" {

void ts_console_error(TsString* str) {
    if (str) {
        std::fprintf(stderr, "%s\n", str->ToUtf8());
    } else {
        std::fprintf(stderr, "undefined\n");
    }
    std::fflush(stderr);
}

void ts_console_error_int(int64_t val) {
    std::fprintf(stderr, "%lld\n", val);
    std::fflush(stderr);
}

void ts_console_error_double(double val) {
    std::fprintf(stderr, "%f\n", val);
    std::fflush(stderr);
}

void ts_console_error_bool(bool val) {
    std::fprintf(stderr, "%s\n", val ? "true" : "false");
    std::fflush(stderr);
}

void ts_console_error_value(TsValue* val) {
    // For now, just use the same logic as log_value but to stderr
    // We should probably implement a proper toString/inspect
    if (!val) {
        std::fprintf(stderr, "undefined\n");
        std::fflush(stderr);
        return;
    }
    ts_console_log_value(val); // Fallback to stdout for now if complex
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
    if (str) {
        std::printf("%s\n", str->ToUtf8());
    } else {
        std::printf("undefined\n");
    }
    std::fflush(stdout);
}

void ts_console_log_int(int64_t val) {
    std::printf("%lld\n", val);
    std::fflush(stdout);
}

void ts_console_log_double(double val) {
    std::printf("%f\n", val);
    std::fflush(stdout);
}

void ts_console_log_bool(bool val) {
    std::printf("%s\n", val ? "true" : "false");
    std::fflush(stdout);
}

extern "C" void ts_console_log_value_no_newline(TsValue* val) {
    if (!val) {
        std::printf("undefined");
        return;
    }

    uint32_t magic = *(uint32_t*)val;
    if (magic == 0x53545247) {
        std::printf("%s", ((TsString*)val)->ToUtf8());
        return;
    }
    
    if (magic == 0x41525259) {
        TsArray* arr = (TsArray*)val;
        int64_t len = arr->Length();
        std::printf("[ ");
        for (int64_t i = 0; i < len; i++) {
            void* elem = (void*)arr->Get(i);
            if (i > 0) std::printf(", ");
            if (elem) {
                TsValue* elemVal = (TsValue*)elem;
                if (elemVal->type == ValueType::STRING_PTR && elemVal->ptr_val) {
                    std::printf("'%s'", ((TsString*)elemVal->ptr_val)->ToUtf8());
                } else if (elemVal->type == ValueType::NUMBER_INT) {
                    std::printf("%lld", elemVal->i_val);
                } else if (elemVal->type == ValueType::NUMBER_DBL) {
                    std::printf("%f", elemVal->d_val);
                } else {
                    uint32_t elemMagic = *(uint32_t*)elem;
                    if (elemMagic == 0x53545247) {
                        std::printf("'%s'", ((TsString*)elem)->ToUtf8());
                    } else {
                        std::printf("[object Object]");
                    }
                }
            } else {
                std::printf("undefined");
            }
        }
        std::printf(" ]");
        return;
    }

    switch (val->type) {
        case ValueType::UNDEFINED: std::printf("undefined"); break;
        case ValueType::NUMBER_INT: std::printf("%lld", val->i_val); break;
        case ValueType::NUMBER_DBL: std::printf("%f", val->d_val); break;
        case ValueType::BOOLEAN: std::printf("%s", val->b_val ? "true" : "false"); break;
        case ValueType::STRING_PTR: std::printf("%s", ((TsString*)val->ptr_val)->ToUtf8()); break;
        case ValueType::OBJECT_PTR: std::printf("[object Object]"); break;
        case ValueType::ARRAY_PTR: {
            TsArray* arr = (TsArray*)val->ptr_val;
            int64_t len = arr->Length();
            std::printf("[ ");
            for (int64_t i = 0; i < len; i++) {
                void* elem = (void*)arr->Get(i);
                if (i > 0) std::printf(", ");
                if (elem) {
                    TsValue* elemVal = (TsValue*)elem;
                    if (elemVal->type == ValueType::STRING_PTR && elemVal->ptr_val) {
                        std::printf("'%s'", ((TsString*)elemVal->ptr_val)->ToUtf8());
                    } else if (elemVal->type == ValueType::NUMBER_INT) {
                        std::printf("%lld", elemVal->i_val);
                    } else if (elemVal->type == ValueType::NUMBER_DBL) {
                        std::printf("%f", elemVal->d_val);
                    } else {
                        uint32_t elemMagic = *(uint32_t*)elem;
                        if (elemMagic == 0x53545247) {
                            std::printf("'%s'", ((TsString*)elem)->ToUtf8());
                        } else {
                            std::printf("[object Object]");
                        }
                    }
                } else {
                    std::printf("undefined");
                }
            }
            std::printf(" ]");
            break;
        }
        case ValueType::PROMISE_PTR: std::printf("[object Promise]"); break;
        case ValueType::BIGINT_PTR: std::printf("%sn", ((TsBigInt*)val->ptr_val)->ToString()); break;
        case ValueType::SYMBOL_PTR: {
            TsSymbol* sym = (TsSymbol*)val->ptr_val;
            if (sym->description) {
                std::printf("Symbol(%s)", sym->description->ToUtf8());
            } else {
                std::printf("Symbol()");
            }
            break;
        }
    }
}

extern "C" void ts_console_log_value(TsValue* val) {
    ts_console_log_value_no_newline(val);
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
    // A TsValue has type byte at offset 0, and the valid range is 0-9
    // Magic numbers have much larger first bytes (0x41, 0x42, 0x46, 0x4D, 0x53)
    uint8_t firstByte = *(uint8_t*)val;
    if (firstByte <= 9) {
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
    if (v->type == ValueType::NUMBER_DBL) return v->d_val;
    if (v->type == ValueType::NUMBER_INT) return (double)v->i_val;
    return 0.0;
}

bool ts_value_to_bool(TsValue* v) {
    if (!v) return false;
    
    // Check for raw TsString* (magic at offset 0)
    uint32_t magic = *(uint32_t*)v;
    if (magic == 0x53545247) { // TsString::MAGIC
        TsString* str = (TsString*)v;
        return str->Length() > 0;  // Empty string is falsy
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
            return v->ptr_val != nullptr;
        default: return false;
    }
}

}
