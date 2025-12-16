#include "TsRuntime.h"

extern "C" {

int ts_main(int argc, char** argv, void (*user_main)()) {
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
