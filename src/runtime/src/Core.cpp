#include "TsRuntime.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsWriteStream.h"
#include "TsReadStream.h"
#include <cstdio>
#include <setjmp.h>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <windows.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
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
static TsValue* currentException = nullptr;
static TsValue* process_argv = nullptr;
static TsValue* process_env = nullptr;
static int64_t process_exit_code = 0;
static TsWriteStream* process_stdout = nullptr;
static TsWriteStream* process_stderr = nullptr;
static TsReadStream* process_stdin = nullptr;

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

void ts_process_set_env(const char* key, TsValue* val) {
    void* env = ts_get_process_env();
    TsMap* envMap = (TsMap*)ts_value_get_object((TsValue*)env);
    
    TsString* sVal = (TsString*)ts_value_get_string(val);
    if (sVal) {
        envMap->Set(TsString::Create(key), *val);
#ifdef _MSC_VER
        _putenv_s(key, sVal->ToUtf8());
#else
        setenv(key, sVal->ToUtf8(), 1);
#endif
    }
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

void ts_process_chdir(void* dir) {
    TsString* s = (TsString*)dir;
    if (s) {
        chdir(s->ToUtf8());
    }
}

int64_t ts_process_get_exit_code() {
    return process_exit_code;
}

void ts_process_set_exit_code(int64_t code) {
    process_exit_code = code;
}

void* ts_process_get_platform() {
#ifdef _WIN32
    return ts_value_make_string(TsString::Create("win32"));
#elif __APPLE__
    return ts_value_make_string(TsString::Create("darwin"));
#else
    return ts_value_make_string(TsString::Create("linux"));
#endif
}

void* ts_process_get_arch() {
#ifdef _M_X64
    return ts_value_make_string(TsString::Create("x64"));
#elif _M_ARM64
    return ts_value_make_string(TsString::Create("arm64"));
#else
    return ts_value_make_string(TsString::Create("x64"));
#endif
}

void* ts_process_get_stdout() {
    if (!process_stdout) {
        process_stdout = new (ts_alloc(sizeof(TsWriteStream))) TsWriteStream(1);
    }
    return process_stdout;
}

void* ts_process_get_stderr() {
    if (!process_stderr) {
        process_stderr = new (ts_alloc(sizeof(TsWriteStream))) TsWriteStream(2);
    }
    return process_stderr;
}

void* ts_process_get_stdin() {
    if (!process_stdin) {
        process_stdin = new (ts_alloc(sizeof(TsReadStream))) TsReadStream(0);
    }
    return process_stdin;
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

void ts_throw(TsValue* exception) {
    currentException = exception;
    if (exceptionStack.empty()) {
        fprintf(stderr, "FATAL: Uncaught exception: ");
        ts_console_log_value(exception);
        fprintf(stderr, "\n"); fflush(stderr);
        exit(1);
    }
    ExceptionContext* ctx = exceptionStack.back();
    exceptionStack.pop_back();
    longjmp(ctx->env, 1);
}

void ts_set_exception(TsValue* exception) {
    currentException = exception;
}

TsValue* ts_get_exception() {
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
