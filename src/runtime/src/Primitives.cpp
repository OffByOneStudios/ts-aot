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

}
