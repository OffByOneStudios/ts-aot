#include "TsRuntime.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsObject.h"
#include "TsWriteStream.h"
#include "TsReadStream.h"
#include <cstdio>
#include <setjmp.h>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <uv.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#ifdef _MSC_VER
#include <crtdbg.h>
#include <direct.h>
#define getcwd _getcwd
#define chdir _chdir
extern "C" char** _environ;
#else
#include <unistd.h>
#include <sys/resource.h>
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
static std::chrono::steady_clock::time_point process_start_time;
static char* process_argv0 = nullptr;
static char* process_exec_path = nullptr;
static char process_title[256] = "ts-aot";
static bool process_title_initialized = false;

// Process event handlers (Milestone 102.8)
static std::vector<TsValue*> exit_handlers;
static std::vector<TsValue*> before_exit_handlers;
static std::vector<TsValue*> uncaught_exception_handlers;
static std::vector<TsValue*> warning_handlers;
static TsValue* uncaught_exception_capture_callback = nullptr;

// Event loop handles (Milestone 102.9)
static uv_idle_t* process_ref_handle = nullptr;
static bool process_is_referenced = true;

#ifdef _WIN32
static LONG WINAPI ts_vectored_exception_handler(PEXCEPTION_POINTERS info) {
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = info->ExceptionRecord->ExceptionCode;
    void* addr = info->ExceptionRecord->ExceptionAddress;

    HMODULE mod = nullptr;
    char modName[MAX_PATH] = {0};
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCSTR)addr, &mod) && mod) {
        GetModuleFileNameA(mod, modName, MAX_PATH);
    }

    fprintf(stderr, "[ts-aot] VectoredException: code=0x%08lx addr=%p module=%s\n",
            (unsigned long)code, addr, (modName[0] ? modName : "<unknown>"));
    fflush(stderr);

    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI ts_unhandled_exception_filter(PEXCEPTION_POINTERS info) {
    if (!info || !info->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;

    const DWORD code = info->ExceptionRecord->ExceptionCode;
    void* addr = info->ExceptionRecord->ExceptionAddress;

    HMODULE mod = nullptr;
    char modName[MAX_PATH] = {0};
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCSTR)addr, &mod) && mod) {
        GetModuleFileNameA(mod, modName, MAX_PATH);
    }

    fprintf(stderr, "[ts-aot] UnhandledException: code=0x%08lx addr=%p module=%s\n",
            (unsigned long)code, addr, (modName[0] ? modName : "<unknown>"));
    fflush(stderr);

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#ifdef _MSC_VER
static DWORD ts_last_seh_code = 0;
static void* ts_last_seh_addr = nullptr;

static int ts_seh_filter(EXCEPTION_POINTERS* info, DWORD code) {
    ts_last_seh_code = code;
    ts_last_seh_addr = nullptr;
    if (info && info->ExceptionRecord) {
        ts_last_seh_addr = info->ExceptionRecord->ExceptionAddress;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

extern "C" {

TsValue* ts_debug_marker(TsValue* msg) {
    fprintf(stderr, "[ts-aot] marker: ");
    ts_console_log_value(msg);
    fprintf(stderr, "\n");
    fflush(stderr);
    return ts_value_make_undefined();
}

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

// ============================================================================
// Milestone 102.5: Process Info
// ============================================================================

int64_t ts_process_get_pid() {
#ifdef _WIN32
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

int64_t ts_process_get_ppid() {
#ifdef _WIN32
    // Windows doesn't have a simple getppid(), return 0 as fallback
    return 0;
#else
    return (int64_t)getppid();
#endif
}

void* ts_process_get_version() {
    // Return a Node.js-compatible version string
    return ts_value_make_string(TsString::Create("v20.0.0"));
}

void* ts_process_get_versions() {
    TsMap* versions = TsMap::Create();
    versions->Set(*ts_value_make_string(TsString::Create("node")), *ts_value_make_string(TsString::Create("20.0.0")));
    versions->Set(*ts_value_make_string(TsString::Create("v8")), *ts_value_make_string(TsString::Create("11.3.0")));
    versions->Set(*ts_value_make_string(TsString::Create("uv")), *ts_value_make_string(TsString::Create(uv_version_string())));
    versions->Set(*ts_value_make_string(TsString::Create("icu")), *ts_value_make_string(TsString::Create("73.1")));
    versions->Set(*ts_value_make_string(TsString::Create("ts-aot")), *ts_value_make_string(TsString::Create("1.0.0")));
    return ts_value_make_object(versions);
}

void* ts_process_get_argv0() {
    if (process_argv0) {
        return ts_value_make_string(TsString::Create(process_argv0));
    }
    return ts_value_make_string(TsString::Create(""));
}

void* ts_process_get_exec_path() {
    if (process_exec_path) {
        return ts_value_make_string(TsString::Create(process_exec_path));
    }
    
    char path[4096] = {0};
    size_t size = sizeof(path);
    if (uv_exepath(path, &size) == 0) {
        return ts_value_make_string(TsString::Create(path));
    }
    return ts_value_make_string(TsString::Create(""));
}

void* ts_process_get_exec_argv() {
    // AOT compiled apps don't have interpreter arguments, return empty array
    TsArray* arr = TsArray::Create(0);
    return ts_value_make_object(arr);
}

void* ts_process_get_title() {
    if (!process_title_initialized) {
        char title[256] = {0};
        size_t size = sizeof(title);
        if (uv_get_process_title(title, size) == 0) {
            strncpy(process_title, title, sizeof(process_title) - 1);
        }
        process_title_initialized = true;
    }
    return ts_value_make_string(TsString::Create(process_title));
}

void ts_process_set_title(void* title) {
    TsString* s = (TsString*)ts_value_get_object((TsValue*)title);
    if (!s) s = (TsString*)title;
    if (s) {
        const char* str = s->ToUtf8();
        strncpy(process_title, str, sizeof(process_title) - 1);
        uv_set_process_title(str);
        process_title_initialized = true;
    }
}

// ============================================================================
// Milestone 102.6: High-Resolution Time & Resource Usage
// ============================================================================

void* ts_process_hrtime(void* prevTime) {
    uint64_t now = uv_hrtime();
    
    if (prevTime) {
        TsArray* prev = (TsArray*)ts_value_get_object((TsValue*)prevTime);
        if (!prev) prev = (TsArray*)prevTime;
        if (prev && prev->Length() >= 2) {
            // TsArray::Get returns raw int64_t, not boxed TsValue*
            int64_t prevSec = prev->Get(0);
            int64_t prevNsec = prev->Get(1);
            uint64_t prevTotal = (uint64_t)prevSec * 1000000000ULL + (uint64_t)prevNsec;
            now = now - prevTotal;
        }
    }
    
    int64_t seconds = (int64_t)(now / 1000000000ULL);
    int64_t nanoseconds = (int64_t)(now % 1000000000ULL);
    
    TsArray* result = TsArray::Create(2);
    result->Push(seconds);
    result->Push(nanoseconds);
    return ts_value_make_object(result);
}

int64_t ts_process_hrtime_bigint() {
    return (int64_t)uv_hrtime();
}

double ts_process_uptime() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - process_start_time);
    return (double)duration.count() / 1000.0;
}

void* ts_process_memory_usage() {
    TsMap* usage = TsMap::Create();
    
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        usage->Set(*ts_value_make_string(TsString::Create("rss")), *ts_value_make_int((int64_t)pmc.WorkingSetSize));
        usage->Set(*ts_value_make_string(TsString::Create("heapTotal")), *ts_value_make_int((int64_t)pmc.PagefileUsage));
        usage->Set(*ts_value_make_string(TsString::Create("heapUsed")), *ts_value_make_int((int64_t)pmc.WorkingSetSize));
    } else {
        usage->Set(*ts_value_make_string(TsString::Create("rss")), *ts_value_make_int(0));
        usage->Set(*ts_value_make_string(TsString::Create("heapTotal")), *ts_value_make_int(0));
        usage->Set(*ts_value_make_string(TsString::Create("heapUsed")), *ts_value_make_int(0));
    }
#else
    size_t rss = 0;
    uv_resident_set_memory(&rss);
    usage->Set(*ts_value_make_string(TsString::Create("rss")), *ts_value_make_int((int64_t)rss));
    usage->Set(*ts_value_make_string(TsString::Create("heapTotal")), *ts_value_make_int((int64_t)rss));
    usage->Set(*ts_value_make_string(TsString::Create("heapUsed")), *ts_value_make_int((int64_t)rss));
#endif
    
    usage->Set(*ts_value_make_string(TsString::Create("external")), *ts_value_make_int(0));
    usage->Set(*ts_value_make_string(TsString::Create("arrayBuffers")), *ts_value_make_int(0));
    
    return ts_value_make_object(usage);
}

int64_t ts_process_memory_usage_rss() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (int64_t)pmc.WorkingSetSize;
    }
    return 0;
#else
    size_t rss = 0;
    uv_resident_set_memory(&rss);
    return (int64_t)rss;
#endif
}

void* ts_process_cpu_usage(void* prevUsage) {
    uv_rusage_t rusage;
    if (uv_getrusage(&rusage) != 0) {
        TsMap* result = TsMap::Create();
        result->Set(*ts_value_make_string(TsString::Create("user")), *ts_value_make_int(0));
        result->Set(*ts_value_make_string(TsString::Create("system")), *ts_value_make_int(0));
        return ts_value_make_object(result);
    }
    
    int64_t userMicros = (int64_t)rusage.ru_utime.tv_sec * 1000000 + (int64_t)rusage.ru_utime.tv_usec;
    int64_t systemMicros = (int64_t)rusage.ru_stime.tv_sec * 1000000 + (int64_t)rusage.ru_stime.tv_usec;
    
    if (prevUsage) {
        TsMap* prev = (TsMap*)ts_value_get_object((TsValue*)prevUsage);
        if (!prev) prev = (TsMap*)prevUsage;
        if (prev) {
            TsValue userVal = prev->Get(*ts_value_make_string(TsString::Create("user")));
            TsValue sysVal = prev->Get(*ts_value_make_string(TsString::Create("system")));
            int64_t prevUser = ts_value_get_int(&userVal);
            int64_t prevSys = ts_value_get_int(&sysVal);
            userMicros -= prevUser;
            systemMicros -= prevSys;
        }
    }
    
    TsMap* result = TsMap::Create();
    result->Set(*ts_value_make_string(TsString::Create("user")), *ts_value_make_int(userMicros));
    result->Set(*ts_value_make_string(TsString::Create("system")), *ts_value_make_int(systemMicros));
    return ts_value_make_object(result);
}

void* ts_process_resource_usage() {
    uv_rusage_t rusage;
    TsMap* result = TsMap::Create();
    
    if (uv_getrusage(&rusage) != 0) {
        return ts_value_make_object(result);
    }
    
    result->Set(*ts_value_make_string(TsString::Create("userCPUTime")), *ts_value_make_int((int64_t)rusage.ru_utime.tv_sec * 1000000 + (int64_t)rusage.ru_utime.tv_usec));
    result->Set(*ts_value_make_string(TsString::Create("systemCPUTime")), *ts_value_make_int((int64_t)rusage.ru_stime.tv_sec * 1000000 + (int64_t)rusage.ru_stime.tv_usec));
    result->Set(*ts_value_make_string(TsString::Create("maxRSS")), *ts_value_make_int((int64_t)rusage.ru_maxrss));
    result->Set(*ts_value_make_string(TsString::Create("sharedMemorySize")), *ts_value_make_int(0));
    result->Set(*ts_value_make_string(TsString::Create("unsharedDataSize")), *ts_value_make_int(0));
    result->Set(*ts_value_make_string(TsString::Create("unsharedStackSize")), *ts_value_make_int(0));
    result->Set(*ts_value_make_string(TsString::Create("minorPageFault")), *ts_value_make_int((int64_t)rusage.ru_minflt));
    result->Set(*ts_value_make_string(TsString::Create("majorPageFault")), *ts_value_make_int((int64_t)rusage.ru_majflt));
    result->Set(*ts_value_make_string(TsString::Create("swappedOut")), *ts_value_make_int((int64_t)rusage.ru_nswap));
    result->Set(*ts_value_make_string(TsString::Create("fsRead")), *ts_value_make_int((int64_t)rusage.ru_inblock));
    result->Set(*ts_value_make_string(TsString::Create("fsWrite")), *ts_value_make_int((int64_t)rusage.ru_oublock));
    result->Set(*ts_value_make_string(TsString::Create("ipcSent")), *ts_value_make_int((int64_t)rusage.ru_msgsnd));
    result->Set(*ts_value_make_string(TsString::Create("ipcReceived")), *ts_value_make_int((int64_t)rusage.ru_msgrcv));
    result->Set(*ts_value_make_string(TsString::Create("signalsCount")), *ts_value_make_int((int64_t)rusage.ru_nsignals));
    result->Set(*ts_value_make_string(TsString::Create("voluntaryContextSwitches")), *ts_value_make_int((int64_t)rusage.ru_nvcsw));
    result->Set(*ts_value_make_string(TsString::Create("involuntaryContextSwitches")), *ts_value_make_int((int64_t)rusage.ru_nivcsw));
    
    return ts_value_make_object(result);
}

// ============================================================================
// Milestone 102.7: Process Control
// ============================================================================

int32_t ts_process_kill(int64_t pid, int32_t signal) {
    return uv_kill((int)pid, signal);
}

void ts_process_abort() {
    abort();
}

int32_t ts_process_umask(int32_t mask) {
#ifdef _WIN32
    // Windows doesn't support umask, return 0
    (void)mask;
    return 0;
#else
    if (mask < 0) {
        // Get current umask
        mode_t current = umask(0);
        umask(current);
        return (int32_t)current;
    }
    return (int32_t)umask((mode_t)mask);
#endif
}

void ts_process_emit_warning(void* warning) {
    TsString* msg = (TsString*)ts_value_get_object((TsValue*)warning);
    if (!msg) msg = (TsString*)warning;
    if (msg) {
        fprintf(stderr, "(node:process) Warning: %s\n", msg->ToUtf8());
    }
}

// ============================================================================
// Milestone 102.10: Configuration & Features
// ============================================================================

void* ts_process_get_config() {
    TsMap* config = TsMap::Create();
    TsMap* variables = TsMap::Create();
    
    // Add some basic config info
    variables->Set(*ts_value_make_string(TsString::Create("host_arch")), *ts_value_make_string(TsString::Create(
#ifdef _M_X64
        "x64"
#elif _M_ARM64
        "arm64"
#else
        "x64"
#endif
    )));
    
    variables->Set(*ts_value_make_string(TsString::Create("host_os")), *ts_value_make_string(TsString::Create(
#ifdef _WIN32
        "win32"
#elif __APPLE__
        "darwin"
#else
        "linux"
#endif
    )));
    
    config->Set(*ts_value_make_string(TsString::Create("variables")), *ts_value_make_object(variables));
    return ts_value_make_object(config);
}

void* ts_process_get_features() {
    TsMap* features = TsMap::Create();
    features->Set(*ts_value_make_string(TsString::Create("inspector")), *ts_value_make_bool(false));
    features->Set(*ts_value_make_string(TsString::Create("debug")), *ts_value_make_bool(false));
    features->Set(*ts_value_make_string(TsString::Create("uv")), *ts_value_make_bool(true));
    features->Set(*ts_value_make_string(TsString::Create("ipv6")), *ts_value_make_bool(true));
    features->Set(*ts_value_make_string(TsString::Create("tls_alpn")), *ts_value_make_bool(false));
    features->Set(*ts_value_make_string(TsString::Create("tls_sni")), *ts_value_make_bool(false));
    features->Set(*ts_value_make_string(TsString::Create("tls_ocsp")), *ts_value_make_bool(false));
    features->Set(*ts_value_make_string(TsString::Create("tls")), *ts_value_make_bool(false));
    return ts_value_make_object(features);
}

void* ts_process_get_release() {
    TsMap* release = TsMap::Create();
    release->Set(*ts_value_make_string(TsString::Create("name")), *ts_value_make_string(TsString::Create("ts-aot")));
    release->Set(*ts_value_make_string(TsString::Create("lts")), *ts_value_make_string(TsString::Create("")));
    return ts_value_make_object(release);
}

int64_t ts_process_get_debug_port() {
    return 9229; // Default Node.js debug port
}

// ============================================================================
// Milestone 102.8: Process Events
// ============================================================================

void ts_process_on(void* event, void* callback) {
    TsString* eventName = (TsString*)ts_value_get_string((TsValue*)event);
    if (!eventName) eventName = (TsString*)event;
    if (!eventName) return;
    
    const char* name = eventName->ToUtf8();
    TsValue* cb = (TsValue*)callback;
    
    if (strcmp(name, "exit") == 0) {
        exit_handlers.push_back(cb);
    } else if (strcmp(name, "beforeExit") == 0) {
        before_exit_handlers.push_back(cb);
    } else if (strcmp(name, "uncaughtException") == 0) {
        uncaught_exception_handlers.push_back(cb);
    } else if (strcmp(name, "warning") == 0) {
        warning_handlers.push_back(cb);
    }
}

void ts_process_once(void* event, void* callback) {
    // For simplicity, once just calls on - a proper implementation would remove after first call
    ts_process_on(event, callback);
}

void ts_process_remove_listener(void* event, void* callback) {
    TsString* eventName = (TsString*)ts_value_get_string((TsValue*)event);
    if (!eventName) eventName = (TsString*)event;
    if (!eventName) return;
    
    const char* name = eventName->ToUtf8();
    TsValue* cb = (TsValue*)callback;
    
    std::vector<TsValue*>* handlers = nullptr;
    if (strcmp(name, "exit") == 0) {
        handlers = &exit_handlers;
    } else if (strcmp(name, "beforeExit") == 0) {
        handlers = &before_exit_handlers;
    } else if (strcmp(name, "uncaughtException") == 0) {
        handlers = &uncaught_exception_handlers;
    } else if (strcmp(name, "warning") == 0) {
        handlers = &warning_handlers;
    }
    
    if (handlers) {
        handlers->erase(std::remove(handlers->begin(), handlers->end(), cb), handlers->end());
    }
}

void ts_process_remove_all_listeners(void* event) {
    if (!event) {
        exit_handlers.clear();
        before_exit_handlers.clear();
        uncaught_exception_handlers.clear();
        warning_handlers.clear();
        return;
    }
    
    TsString* eventName = (TsString*)ts_value_get_string((TsValue*)event);
    if (!eventName) eventName = (TsString*)event;
    if (!eventName) return;
    
    const char* name = eventName->ToUtf8();
    
    if (strcmp(name, "exit") == 0) {
        exit_handlers.clear();
    } else if (strcmp(name, "beforeExit") == 0) {
        before_exit_handlers.clear();
    } else if (strcmp(name, "uncaughtException") == 0) {
        uncaught_exception_handlers.clear();
    } else if (strcmp(name, "warning") == 0) {
        warning_handlers.clear();
    }
}

void ts_process_set_uncaught_exception_capture_callback(void* callback) {
    uncaught_exception_capture_callback = (TsValue*)callback;
}

bool ts_process_has_uncaught_exception_capture_callback() {
    return uncaught_exception_capture_callback != nullptr;
}

// Helper to invoke exit handlers - called from ts_process_exit
static void invoke_exit_handlers(int64_t code) {
    TsValue* codeVal = ts_value_make_int(code);
    TsValue* args[1] = { codeVal };
    for (auto handler : exit_handlers) {
        ts_function_call(handler, 1, args);
    }
}

// ============================================================================
// Milestone 102.9: Event Loop Handles
// ============================================================================

static void process_ref_cb(uv_idle_t* handle) {
    // Just keep event loop alive, do nothing
    (void)handle;
}

void ts_process_ref() {
    if (!process_ref_handle) {
        process_ref_handle = (uv_idle_t*)malloc(sizeof(uv_idle_t));
        uv_loop_t* loop = uv_default_loop();
        uv_idle_init(loop, process_ref_handle);
        uv_idle_start(process_ref_handle, process_ref_cb);
    }
    process_is_referenced = true;
}

void ts_process_unref() {
    if (process_ref_handle) {
        uv_idle_stop(process_ref_handle);
        uv_close((uv_handle_t*)process_ref_handle, [](uv_handle_t* h) { free(h); });
        process_ref_handle = nullptr;
    }
    process_is_referenced = false;
}

void* ts_process_get_active_resources_info() {
    TsArray* result = TsArray::Create(0);
    uv_loop_t* loop = uv_default_loop();
    
    // Walk all handles in the loop
    uv_walk(loop, [](uv_handle_t* handle, void* arg) {
        TsArray* arr = (TsArray*)arg;
        const char* type = nullptr;
        
        switch (handle->type) {
            case UV_TCP: type = "TCPSocket"; break;
            case UV_UDP: type = "UDPSocket"; break;
            case UV_NAMED_PIPE: type = "Pipe"; break;
            case UV_TTY: type = "TTY"; break;
            case UV_TIMER: type = "Timeout"; break;
            case UV_PREPARE: type = "Prepare"; break;
            case UV_CHECK: type = "Check"; break;
            case UV_IDLE: type = "Idle"; break;
            case UV_ASYNC: type = "Async"; break;
            case UV_POLL: type = "Poll"; break;
            case UV_SIGNAL: type = "Signal"; break;
            case UV_FS_EVENT: type = "FSEvent"; break;
            case UV_FS_POLL: type = "FSPoll"; break;
            default: type = "Unknown"; break;
        }
        
        arr->Push((int64_t)ts_value_make_string(TsString::Create(type)));
    }, result);
    
    return ts_value_make_object(result);
}

// ============================================================================
// Milestone 102.12: Memory Info
// ============================================================================

void* ts_process_constrained_memory() {
    // Returns memory limit (cgroups on Linux) or undefined
    // On Windows, return undefined (null)
    return ts_value_make_undefined();
}

void* ts_process_available_memory() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return ts_value_make_int((int64_t)memInfo.ullAvailPhys);
    }
    return ts_value_make_int(0);
#else
    // On Unix, could use sysconf or parse /proc/meminfo
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return ts_value_make_int((int64_t)(pages * page_size));
#endif
}

// ============================================================================
// Milestone 102.13: Internal/Debug APIs (Stubs)
// ============================================================================

void* ts_process_get_active_handles() {
    // Return empty array - internal API stub
    return ts_value_make_object(TsArray::Create(0));
}

void* ts_process_get_active_requests() {
    // Return empty array - internal API stub
    return ts_value_make_object(TsArray::Create(0));
}

void ts_process_tick_callback() {
    // Process microtask queue - stub
    ts_run_microtasks();
}

// ============================================================================
// Milestone 102.14: Diagnostics & Reporting
// ============================================================================

void* ts_process_get_report() {
    TsMap* report = TsMap::Create();
    
    // Header info
    TsMap* header = TsMap::Create();
    header->Set(*ts_value_make_string(TsString::Create("reportVersion")), *ts_value_make_int(1));
    header->Set(*ts_value_make_string(TsString::Create("nodejsVersion")), *ts_value_make_string(TsString::Create("v20.0.0")));
    header->Set(*ts_value_make_string(TsString::Create("platform")), *ts_value_make_string(TsString::Create(
#ifdef _WIN32
        "win32"
#elif __APPLE__
        "darwin"
#else
        "linux"
#endif
    )));
    report->Set(*ts_value_make_string(TsString::Create("header")), *ts_value_make_object(header));
    
    return ts_value_make_object(report);
}

void* ts_process_report_get_report() {
    return ts_process_get_report();
}

void ts_process_report_write_report(void* filename) {
    // Write report to file - stub for now
    (void)filename;
    fprintf(stderr, "Report writing is not implemented\n");
}

void* ts_process_report_get_directory() {
    return ts_value_make_string(TsString::Create(""));
}

void ts_process_report_set_directory(void* dir) {
    (void)dir;
}

void* ts_process_report_get_filename() {
    return ts_value_make_string(TsString::Create(""));
}

void ts_process_report_set_filename(void* filename) {
    (void)filename;
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

#ifdef _WIN32
    PVOID veh = AddVectoredExceptionHandler(1, ts_vectored_exception_handler);
    if (!veh) {
        const DWORD err = GetLastError();
        fprintf(stderr, "[ts-aot] ts_main: AddVectoredExceptionHandler failed err=%lu\n", (unsigned long)err);
        fflush(stderr);
    }
    SetUnhandledExceptionFilter(ts_unhandled_exception_filter);
#endif

    // 0. Record process start time and argv0
    process_start_time = std::chrono::steady_clock::now();
    if (argc > 0 && argv[0]) {
        process_argv0 = argv[0];
        process_exec_path = argv[0];
    }

    // 1. Initialize Garbage Collector
    ts_gc_init();

    // 1.5 Initialize Runtime Globals
    ts_runtime_init();

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

#ifdef _MSC_VER
        __try {
            TsValue* result = user_main(nullptr);
            (void)result; // For now, we don't do anything special with the top-level promise
        } __except(ts_seh_filter(GetExceptionInformation(), GetExceptionCode())) {
            fprintf(stderr, "[ts-aot] EXCEPTION code=0x%08lx addr=%p\n",
                    (unsigned long)ts_last_seh_code, ts_last_seh_addr);
            fflush(stderr);
            return 1;
        }
#else
        TsValue* result = user_main(nullptr);
        (void)result; // For now, we don't do anything special with the top-level promise
#endif
    }

    // 5. Run Event Loop
    ts_loop_run();

    return 0;
}

}
