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

}
