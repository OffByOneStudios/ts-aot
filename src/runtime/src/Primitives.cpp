#include "TsRuntime.h"
#include "TsString.h"
#include <cstdio>
#include <cmath>

extern "C" {

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

void ts_console_log_value(TsValue* val) {
    if (!val) {
        std::printf("undefined\n");
        std::fflush(stdout);
        return;
    }

    // Check if it's actually a TsString* being passed as a TsValue*
    // A TsString starts with a magic number 0x53545247
    uint32_t magic = *(uint32_t*)val;
    if (magic == 0x53545247) {
        std::printf("%s\n", ((TsString*)val)->ToUtf8());
        std::fflush(stdout);
        return;
    }

    switch (val->type) {
        case ValueType::UNDEFINED: std::printf("undefined\n"); break;
        case ValueType::NUMBER_INT: std::printf("%lld\n", val->i_val); break;
        case ValueType::NUMBER_DBL: std::printf("%f\n", val->d_val); break;
        case ValueType::BOOLEAN: std::printf("%s\n", val->b_val ? "true" : "false"); break;
        case ValueType::STRING_PTR: std::printf("%s\n", ((TsString*)val->ptr_val)->ToUtf8()); break;
        case ValueType::OBJECT_PTR: std::printf("[object Object]\n"); break;
        case ValueType::ARRAY_PTR: std::printf("[object Array]\n"); break;
        case ValueType::PROMISE_PTR: std::printf("[object Promise]\n"); break;
    }
    std::fflush(stdout);
}

TsString* ts_typeof(void* val) {
    if (!val) return TsString::Create("undefined");
    
    uintptr_t ptr = (uintptr_t)val;
    if (ptr > 0x00007FFFFFFFFFFF) {
        return TsString::Create("number");
    }
    
    // Check for TsString magic number
    // Safety: in our runtime, pointers are either to TsString or TsObject which are both > 4 bytes.
    uint32_t magic = *(uint32_t*)val;
    if (magic == 0x53545247) {
        return TsString::Create("string");
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
    switch (v->type) {
        case ValueType::UNDEFINED: return false;
        case ValueType::NUMBER_INT: return v->i_val != 0;
        case ValueType::NUMBER_DBL: return v->d_val != 0.0;
        case ValueType::BOOLEAN: return v->b_val;
        case ValueType::STRING_PTR: return v->ptr_val != nullptr;
        case ValueType::OBJECT_PTR:
        case ValueType::PROMISE_PTR:
        case ValueType::ARRAY_PTR:
            return v->ptr_val != nullptr;
        default: return false;
    }
}

}
