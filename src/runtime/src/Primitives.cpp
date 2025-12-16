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

}
