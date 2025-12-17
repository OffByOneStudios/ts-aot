#include "TsRuntime.h"
#include <cstdio>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

extern "C" {

int ts_main(int argc, char** argv, void (*user_main)()) {
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif

    // 1. Initialize Garbage Collector
    ts_gc_init();

    // 2. Initialize Event Loop
    ts_loop_init();

    // 3. Run User Code (which might schedule async work)
    if (user_main) {
        user_main();
    }

    // 4. Run Event Loop
    ts_loop_run();

    return 0;
}

}
