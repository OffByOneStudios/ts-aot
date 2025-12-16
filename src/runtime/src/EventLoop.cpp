#include "TsRuntime.h"
#include <uv.h>
#include <cstdio>

// Global event loop
uv_loop_t* g_loop = nullptr;

extern "C" {

void ts_loop_init() {
    if (g_loop == nullptr) {
        g_loop = uv_default_loop();
    }
}

void ts_loop_run() {
    if (g_loop) {
        uv_run(g_loop, UV_RUN_DEFAULT);
    }
}

}
