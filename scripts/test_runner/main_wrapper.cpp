#include <stdio.h>
#include <stdlib.h>

// Forward declare ts_aot_main from the generated object file
extern "C" int ts_aot_main(int argc, char** argv);

int main(int argc, char** argv) {
    // fprintf(stderr, "[Wrapper] Starting execution\n");
    // fflush(stderr);
    
    int ret = ts_aot_main(argc, argv);
    
    // fprintf(stderr, "[Wrapper] Exiting with %d\n", ret);
    return ret;
}
