#include "TsRuntime.h"
#include <cstdio>
#include <setjmp.h>
#include <vector>
#include <cstdlib>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

struct ExceptionContext {
    jmp_buf env;
};

static std::vector<ExceptionContext*> exceptionStack;
static void* currentException = nullptr;

extern "C" {

void* ts_push_exception_handler() {
    ExceptionContext* ctx = (ExceptionContext*)malloc(sizeof(ExceptionContext));
    exceptionStack.push_back(ctx);
    return (void*)ctx->env;
}

void ts_pop_exception_handler() {
    if (!exceptionStack.empty()) {
        ExceptionContext* ctx = exceptionStack.back();
        exceptionStack.pop_back();
        free(ctx);
    }
}

void ts_throw(void* exception) {
    currentException = exception;
    if (exceptionStack.empty()) {
        fprintf(stderr, "Uncaught exception\n");
        exit(1);
    }
    ExceptionContext* ctx = exceptionStack.back();
    exceptionStack.pop_back();
    longjmp(ctx->env, 1);
}

void* ts_get_exception() {
    return currentException;
}

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
