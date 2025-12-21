#include "TsRuntime.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsMap.h"
#include <cstdio>
#include <setjmp.h>
#include <vector>
#include <cstdlib>
#include <filesystem>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <direct.h>
#define getcwd _getcwd
extern "C" char** _environ;
#else
#include <unistd.h>
extern "C" char** environ;
#endif

namespace fs = std::filesystem;

struct ExceptionContext {
    jmp_buf env;
};

static std::vector<ExceptionContext*> exceptionStack;
static void* currentException = nullptr;
static TsValue* process_argv = nullptr;
static TsValue* process_env = nullptr;

extern "C" {

void* ts_get_process_argv() {
    if (!process_argv || process_argv->type != ValueType::ARRAY_PTR) return nullptr;
    return process_argv->ptr_val;
}

void* ts_get_process_env() {
    if (!process_env) {
        TsMap* envMap = TsMap::Create();
#ifdef _MSC_VER
        char** env = _environ;
#else
        char** env = environ;
#endif
        if (env) {
            for (char** s = env; *s; s++) {
                std::string entry(*s);
                size_t pos = entry.find('=');
                if (pos != std::string::npos) {
                    std::string key = entry.substr(0, pos);
                    std::string val = entry.substr(pos + 1);
                    envMap->Set(TsString::Create(key.c_str()), *ts_value_make_string(ts_string_create(val.c_str())));
                }
            }
        }
        process_env = ts_value_make_object(envMap);
    }
    return process_env;
}

void ts_process_exit(int64_t code) {
    exit((int)code);
}

void* ts_process_cwd() {
    char buf[1024];
    if (getcwd(buf, sizeof(buf))) {
        return ts_value_make_string(TsString::Create(buf));
    }
    return ts_value_make_string(TsString::Create(""));
}

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

void ts_set_exception(void* exception) {
    currentException = exception;
}

void* ts_get_exception() {
    return currentException;
}

int ts_main(int argc, char** argv, TsValue* (*user_main)(void*)) {
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

    // 3. Initialize process.argv
    TsArray* argvArray = TsArray::Create(argc);
    for (int i = 0; i < argc; ++i) {
        void* s = ts_string_create(argv[i]);
        argvArray->Push((int64_t)ts_value_make_string(s));
    }
    process_argv = ts_value_make_array(argvArray);

    // 4. Run User Code (which might schedule async work)
    if (user_main) {
        TsValue* result = user_main(nullptr);
        (void)result; // For now, we don't do anything special with the top-level promise
    }

    // 5. Run Event Loop
    ts_loop_run();

    return 0;
}

}
