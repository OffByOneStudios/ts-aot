#include "TsRuntime.h"
#include "TsString.h"
#include <cstdio>

extern "C" {

void ts_console_log(TsString* str) {
    if (str) {
        std::printf("%s\n", str->ToUtf8());
    } else {
        std::printf("undefined\n");
    }
}

void ts_console_log_int(int64_t val) {
    std::printf("%lld\n", val);
}

void ts_console_log_double(double val) {
    std::printf("%f\n", val);
}

void ts_console_log_bool(bool val) {
    std::printf("%s\n", val ? "true" : "false");
}

void ts_console_log_any(TsValue* val) {
    if (!val) {
        std::printf("undefined\n");
        return;
    }
    switch (val->type) {
        case ValueType::NUMBER_INT: std::printf("%lld\n", val->i_val); break;
        case ValueType::NUMBER_DBL: std::printf("%f\n", val->d_val); break;
        case ValueType::BOOLEAN: std::printf("%s\n", val->b_val ? "true" : "false"); break;
        case ValueType::STRING_PTR: {
            TsString* s = (TsString*)val->ptr_val;
            std::printf("%s\n", s ? s->ToUtf8() : "null");
            break;
        }
        case ValueType::OBJECT_PTR: std::printf("[object Object]\n"); break;
        case ValueType::UNDEFINED: std::printf("undefined\n"); break;
        default: std::printf("[unknown]\n"); break;
    }
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

}
