#include "TsRuntime.h"
#include "TsArray.h"
#include "TsString.h"
#include "TsMap.h"
#include "TsObject.h"
#include "TsWriteStream.h"
#include "TsReadStream.h"
#include "TsGC.h"
#include "TsNanBox.h"
// TsCluster.h removed - cluster init is now done via ts_node_init_hook
#include <cstdio>
#include <setjmp.h>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <uv.h>
#include <unicode/uclean.h>
#include <unicode/putil.h>
#include <unicode/utypes.h>
#include <unicode/udata.h>

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

// Node.js init hook - set by nodecore (TsCluster.cpp) via static initializer
TsNodeInitHook ts_node_init_hook = nullptr;

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
static std::vector<TsValue*> sigint_handlers;
static std::vector<TsValue*> sigterm_handlers;
static uv_signal_t* sigint_watcher = nullptr;
static uv_signal_t* sigterm_watcher = nullptr;
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

    // On access violation, dump registers and disassembly for nursery GC debugging
    if (code == 0xC0000005 && info->ContextRecord) {
        CONTEXT* ctx = info->ContextRecord;
        // Print the faulting address from exception params
        if (info->ExceptionRecord->NumberParameters >= 2) {
            fprintf(stderr, "[ts-aot] Fault: %s address %p\n",
                    info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" : "writing",
                    (void*)info->ExceptionRecord->ExceptionInformation[1]);
        }
        fprintf(stderr, "[ts-aot] Registers:\n");
        fprintf(stderr, "  RAX=%016llX  RBX=%016llX  RCX=%016llX\n", ctx->Rax, ctx->Rbx, ctx->Rcx);
        fprintf(stderr, "  RDX=%016llX  RSI=%016llX  RDI=%016llX\n", ctx->Rdx, ctx->Rsi, ctx->Rdi);
        fprintf(stderr, "  RSP=%016llX  RBP=%016llX  RIP=%016llX\n", ctx->Rsp, ctx->Rbp, ctx->Rip);
        fprintf(stderr, "  R8 =%016llX  R9 =%016llX  R10=%016llX\n", ctx->R8, ctx->R9, ctx->R10);
        fprintf(stderr, "  R11=%016llX  R12=%016llX  R13=%016llX\n", ctx->R11, ctx->R12, ctx->R13);
        fprintf(stderr, "  R14=%016llX  R15=%016llX\n", ctx->R14, ctx->R15);

        // Print instruction bytes at crash site
        fprintf(stderr, "[ts-aot] Instruction bytes at RIP:");
        unsigned char* ip = (unsigned char*)(uintptr_t)ctx->Rip;
        for (int i = 0; i < 16; i++) {
            fprintf(stderr, " %02X", ip[i]);
        }
        fprintf(stderr, "\n");

        // Print stack words around RSP
        fprintf(stderr, "[ts-aot] Stack near RSP:\n");
        uint64_t* sp = (uint64_t*)(uintptr_t)ctx->Rsp;
        for (int i = -2; i < 16; i++) {
            fprintf(stderr, "  [RSP%+d] = %016llX\n", i*8, sp[i]);
        }

        // Check which registers point into nursery
        void* nursery_base = nullptr;
        size_t nursery_size = 0;
        ts_gc_nursery_info(&nursery_base, &nursery_size);
        if (nursery_base) {
            fprintf(stderr, "[ts-aot] Nursery: base=%p size=%zuKB end=%p\n",
                    nursery_base, nursery_size / 1024,
                    (void*)((char*)nursery_base + nursery_size));
        }

        DWORD64 regs[] = { ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx, ctx->Rsi, ctx->Rdi,
                           ctx->R8, ctx->R9, ctx->R10, ctx->R11, ctx->R12, ctx->R13,
                           ctx->R14, ctx->R15, ctx->Rbp };
        const char* names[] = { "RAX", "RBX", "RCX", "RDX", "RSI", "RDI",
                                "R8", "R9", "R10", "R11", "R12", "R13",
                                "R14", "R15", "RBP" };
        for (int i = 0; i < 15; i++) {
            if (regs[i] > 4096 && regs[i] <= 0x00007FFFFFFFFFFF) {
                if (ts_gc_is_nursery((void*)regs[i])) {
                    size_t off = (size_t)((char*)(uintptr_t)regs[i] - (char*)nursery_base);
                    fprintf(stderr, "  ** %s=%016llX is NURSERY (offset=%zu) **\n", names[i], regs[i], off);
                }
            }
        }
        // Also check faulting address
        if (info->ExceptionRecord->NumberParameters >= 2) {
            DWORD64 faultAddr = info->ExceptionRecord->ExceptionInformation[1];
            if (faultAddr > 4096 && faultAddr <= 0x00007FFFFFFFFFFF && ts_gc_is_nursery((void*)faultAddr)) {
                size_t off = (size_t)((char*)(uintptr_t)faultAddr - (char*)nursery_base);
                fprintf(stderr, "  ** Faulting address %016llX is NURSERY (offset=%zu) **\n", faultAddr, off);
            }
        }
    }

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
    if (!process_argv) return nullptr;
    uint64_t nb = nanbox_from_tsvalue_ptr(process_argv);
    if (!nanbox_is_ptr(nb)) return nullptr;
    return nanbox_to_ptr(nb);
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
                    envMap->Set(TsString::Create(key.c_str()), nanbox_to_tagged(ts_value_make_string(ts_string_create(val.c_str()))));
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
    versions->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("node"))), nanbox_to_tagged(ts_value_make_string(TsString::Create("20.0.0"))));
    versions->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("v8"))), nanbox_to_tagged(ts_value_make_string(TsString::Create("11.3.0"))));
    versions->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("uv"))), nanbox_to_tagged(ts_value_make_string(TsString::Create(uv_version_string()))));
    versions->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("icu"))), nanbox_to_tagged(ts_value_make_string(TsString::Create("73.1"))));
    versions->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("ts-aot"))), nanbox_to_tagged(ts_value_make_string(TsString::Create("1.0.0"))));
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
        usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("rss"))), nanbox_to_tagged(ts_value_make_int((int64_t)pmc.WorkingSetSize)));
        usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("heapTotal"))), nanbox_to_tagged(ts_value_make_int((int64_t)pmc.PagefileUsage)));
        usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("heapUsed"))), nanbox_to_tagged(ts_value_make_int((int64_t)pmc.WorkingSetSize)));
    } else {
        usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("rss"))), nanbox_to_tagged(ts_value_make_int(0)));
        usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("heapTotal"))), nanbox_to_tagged(ts_value_make_int(0)));
        usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("heapUsed"))), nanbox_to_tagged(ts_value_make_int(0)));
    }
#else
    size_t rss = 0;
    uv_resident_set_memory(&rss);
    usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("rss"))), nanbox_to_tagged(ts_value_make_int((int64_t)rss)));
    usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("heapTotal"))), nanbox_to_tagged(ts_value_make_int((int64_t)rss)));
    usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("heapUsed"))), nanbox_to_tagged(ts_value_make_int((int64_t)rss)));
#endif
    
    usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("external"))), nanbox_to_tagged(ts_value_make_int(0)));
    usage->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("arrayBuffers"))), nanbox_to_tagged(ts_value_make_int(0)));
    
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
        result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("user"))), nanbox_to_tagged(ts_value_make_int(0)));
        result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("system"))), nanbox_to_tagged(ts_value_make_int(0)));
        return ts_value_make_object(result);
    }
    
    int64_t userMicros = (int64_t)rusage.ru_utime.tv_sec * 1000000 + (int64_t)rusage.ru_utime.tv_usec;
    int64_t systemMicros = (int64_t)rusage.ru_stime.tv_sec * 1000000 + (int64_t)rusage.ru_stime.tv_usec;
    
    if (prevUsage) {
        TsMap* prev = (TsMap*)ts_value_get_object((TsValue*)prevUsage);
        if (!prev) prev = (TsMap*)prevUsage;
        if (prev) {
            TsValue userVal = prev->Get(nanbox_to_tagged(ts_value_make_string(TsString::Create("user"))));
            TsValue sysVal = prev->Get(nanbox_to_tagged(ts_value_make_string(TsString::Create("system"))));
            // Direct field access — userVal/sysVal are TsValue structs, not NaN-boxed pointers
            int64_t prevUser = userVal.i_val;
            int64_t prevSys = sysVal.i_val;
            userMicros -= prevUser;
            systemMicros -= prevSys;
        }
    }
    
    TsMap* result = TsMap::Create();
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("user"))), nanbox_to_tagged(ts_value_make_int(userMicros)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("system"))), nanbox_to_tagged(ts_value_make_int(systemMicros)));
    return ts_value_make_object(result);
}

void* ts_process_resource_usage() {
    uv_rusage_t rusage;
    TsMap* result = TsMap::Create();
    
    if (uv_getrusage(&rusage) != 0) {
        return ts_value_make_object(result);
    }
    
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("userCPUTime"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_utime.tv_sec * 1000000 + (int64_t)rusage.ru_utime.tv_usec)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("systemCPUTime"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_stime.tv_sec * 1000000 + (int64_t)rusage.ru_stime.tv_usec)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("maxRSS"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_maxrss)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("sharedMemorySize"))), nanbox_to_tagged(ts_value_make_int(0)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("unsharedDataSize"))), nanbox_to_tagged(ts_value_make_int(0)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("unsharedStackSize"))), nanbox_to_tagged(ts_value_make_int(0)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("minorPageFault"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_minflt)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("majorPageFault"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_majflt)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("swappedOut"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_nswap)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("fsRead"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_inblock)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("fsWrite"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_oublock)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("ipcSent"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_msgsnd)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("ipcReceived"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_msgrcv)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("signalsCount"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_nsignals)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("voluntaryContextSwitches"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_nvcsw)));
    result->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("involuntaryContextSwitches"))), nanbox_to_tagged(ts_value_make_int((int64_t)rusage.ru_nivcsw)));
    
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
    variables->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("host_arch"))), nanbox_to_tagged(ts_value_make_string(TsString::Create(
#ifdef _M_X64
        "x64"
#elif _M_ARM64
        "arm64"
#else
        "x64"
#endif
    ))));
    
    variables->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("host_os"))), nanbox_to_tagged(ts_value_make_string(TsString::Create(
#ifdef _WIN32
        "win32"
#elif __APPLE__
        "darwin"
#else
        "linux"
#endif
    ))));
    
    config->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("variables"))), nanbox_to_tagged(ts_value_make_object(variables)));
    return ts_value_make_object(config);
}

void* ts_process_get_features() {
    TsMap* features = TsMap::Create();
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("inspector"))), nanbox_to_tagged(ts_value_make_bool(false)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("debug"))), nanbox_to_tagged(ts_value_make_bool(false)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("uv"))), nanbox_to_tagged(ts_value_make_bool(true)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("ipv6"))), nanbox_to_tagged(ts_value_make_bool(true)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("tls_alpn"))), nanbox_to_tagged(ts_value_make_bool(false)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("tls_sni"))), nanbox_to_tagged(ts_value_make_bool(false)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("tls_ocsp"))), nanbox_to_tagged(ts_value_make_bool(false)));
    features->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("tls"))), nanbox_to_tagged(ts_value_make_bool(false)));
    return ts_value_make_object(features);
}

void* ts_process_get_release() {
    TsMap* release = TsMap::Create();
    release->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("name"))), nanbox_to_tagged(ts_value_make_string(TsString::Create("ts-aot"))));
    release->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("lts"))), nanbox_to_tagged(ts_value_make_string(TsString::Create(""))));
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
    } else if (strcmp(name, "SIGINT") == 0) {
        sigint_handlers.push_back(cb);
        if (!sigint_watcher) {
            sigint_watcher = (uv_signal_t*)malloc(sizeof(uv_signal_t));
            uv_signal_init(uv_default_loop(), sigint_watcher);
            uv_signal_start(sigint_watcher, [](uv_signal_t*, int) {
                for (auto* handler : sigint_handlers) {
                    ts_call_1(handler, ts_value_make_string(TsString::Create("SIGINT")));
                }
            }, SIGINT);
            uv_unref((uv_handle_t*)sigint_watcher);
        }
    } else if (strcmp(name, "SIGTERM") == 0) {
        sigterm_handlers.push_back(cb);
        if (!sigterm_watcher) {
            sigterm_watcher = (uv_signal_t*)malloc(sizeof(uv_signal_t));
            uv_signal_init(uv_default_loop(), sigterm_watcher);
            uv_signal_start(sigterm_watcher, [](uv_signal_t*, int) {
                for (auto* handler : sigterm_handlers) {
                    ts_call_1(handler, ts_value_make_string(TsString::Create("SIGTERM")));
                }
            }, SIGTERM);
            uv_unref((uv_handle_t*)sigterm_watcher);
        }
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
    } else if (strcmp(name, "SIGINT") == 0) {
        handlers = &sigint_handlers;
    } else if (strcmp(name, "SIGTERM") == 0) {
        handlers = &sigterm_handlers;
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
        sigint_handlers.clear();
        sigterm_handlers.clear();
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
    } else if (strcmp(name, "SIGINT") == 0) {
        sigint_handlers.clear();
    } else if (strcmp(name, "SIGTERM") == 0) {
        sigterm_handlers.clear();
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
    header->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("reportVersion"))), nanbox_to_tagged(ts_value_make_int(1)));
    header->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("nodejsVersion"))), nanbox_to_tagged(ts_value_make_string(TsString::Create("v20.0.0"))));
    header->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("platform"))), nanbox_to_tagged(ts_value_make_string(TsString::Create(
#ifdef _WIN32
        "win32"
#elif __APPLE__
        "darwin"
#else
        "linux"
#endif
    ))));
    report->Set(nanbox_to_tagged(ts_value_make_string(TsString::Create("header"))), nanbox_to_tagged(ts_value_make_object(header)));
    
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

// ============================================================================
// Child-side IPC support for fork()
// ============================================================================

// IPC state for child processes
static uv_pipe_t* child_ipc_pipe = nullptr;
static bool child_ipc_connected = false;
static std::string child_ipc_read_buffer;
static std::vector<TsValue*> message_handlers;
static std::vector<TsValue*> disconnect_handlers;

// Forward declarations for JSON functions
extern TsString* ts_json_stringify(TsValue* value);
extern TsValue* ts_json_parse(TsValue* jsonStr);

static void on_child_ipc_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
    buf->base = (char*)ts_alloc(suggested_size);
    buf->len = (unsigned int)suggested_size;
}

static void on_child_ipc_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

static void process_child_ipc_messages() {
    // Process length-prefixed messages from child_ipc_read_buffer
    while (child_ipc_read_buffer.length() >= 4) {
        uint32_t msgLen =
            ((uint8_t)child_ipc_read_buffer[0]) |
            (((uint8_t)child_ipc_read_buffer[1]) << 8) |
            (((uint8_t)child_ipc_read_buffer[2]) << 16) |
            (((uint8_t)child_ipc_read_buffer[3]) << 24);

        if (child_ipc_read_buffer.length() < 4 + msgLen) {
            break;  // Wait for more data
        }

        std::string jsonStr = child_ipc_read_buffer.substr(4, msgLen);
        child_ipc_read_buffer.erase(0, 4 + msgLen);

        // Parse JSON and emit 'message' event
        TsString* jsonTsStr = TsString::Create(jsonStr.c_str());
        TsValue* jsonVal = ts_value_make_string(jsonTsStr);
        TsValue* parsedMsg = ts_json_parse(jsonVal);

        if (parsedMsg) {
            // Call all message handlers
            TsValue* args[] = { parsedMsg };
            for (TsValue* handler : message_handlers) {
                ts_function_call(handler, 1, args);
            }
        }
    }
}

static void on_child_ipc_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        child_ipc_read_buffer.append(buf->base, nread);
        process_child_ipc_messages();
    } else if (nread < 0) {
        if (nread == UV_EOF) {
            child_ipc_connected = false;
            // Call disconnect handlers
            for (TsValue* handler : disconnect_handlers) {
                ts_function_call(handler, 0, nullptr);
            }
        }
    }
}

static void on_child_ipc_write(uv_write_t* req, int status) {
    // Write completed
    (void)req;
    (void)status;
}

// Check if running as a forked child with IPC
bool ts_process_has_ipc() {
    return child_ipc_connected;
}

// Initialize child IPC (called from ts_main if fd 3 is a pipe)
void ts_process_init_child_ipc() {
    // Check if fd 3 is a pipe (IPC channel from parent)
    // On Windows, uv_guess_handle can be unreliable for non-existent handles
    // Only attempt IPC initialization if NODE_CHANNEL_FD env var is set
    // (this is the convention Node.js uses for IPC)
    const char* channelFd = getenv("NODE_CHANNEL_FD");
    if (!channelFd || strcmp(channelFd, "3") != 0) {
        return;  // No IPC channel expected
    }

    uv_handle_type type = uv_guess_handle(3);
    if (type != UV_NAMED_PIPE && type != UV_TCP) {
        return;  // Not a valid IPC handle type
    }

    // Try to initialize the IPC pipe on fd 3
    child_ipc_pipe = (uv_pipe_t*)ts_alloc(sizeof(uv_pipe_t));
    int r = uv_pipe_init(uv_default_loop(), child_ipc_pipe, 1);  // 1 = enable IPC
    if (r != 0) {
        child_ipc_pipe = nullptr;
        return;
    }

    r = uv_pipe_open(child_ipc_pipe, 3);
    if (r != 0) {
        // fd 3 is not a valid pipe - don't close, just nullify
        // (closing a handle that failed to open can cause issues)
        child_ipc_pipe = nullptr;
        return;
    }

    child_ipc_connected = true;

    // Start reading from IPC pipe
    uv_read_start((uv_stream_t*)child_ipc_pipe, on_child_ipc_alloc, on_child_ipc_read);
}

// Send a message to the parent process
bool ts_process_send(void* message) {
    if (!child_ipc_pipe || !child_ipc_connected) {
        return false;
    }

    TsValue* msgVal = (TsValue*)message;
    TsString* jsonStr = ts_json_stringify(msgVal);
    if (!jsonStr) {
        return false;
    }

    const char* jsonData = jsonStr->ToUtf8();
    size_t jsonLen = strlen(jsonData);

    // Create length-prefixed message
    size_t totalLen = 4 + jsonLen;
    char* buffer = (char*)ts_alloc(totalLen);

    buffer[0] = (char)(jsonLen & 0xFF);
    buffer[1] = (char)((jsonLen >> 8) & 0xFF);
    buffer[2] = (char)((jsonLen >> 16) & 0xFF);
    buffer[3] = (char)((jsonLen >> 24) & 0xFF);
    memcpy(buffer + 4, jsonData, jsonLen);

    uv_write_t* req = (uv_write_t*)ts_alloc(sizeof(uv_write_t));
    uv_buf_t buf = uv_buf_init(buffer, (unsigned int)totalLen);
    int r = uv_write(req, (uv_stream_t*)child_ipc_pipe, &buf, 1, on_child_ipc_write);

    return r == 0;
}

// Disconnect from the parent process
void ts_process_disconnect() {
    if (!child_ipc_pipe || !child_ipc_connected) {
        return;
    }

    child_ipc_connected = false;
    uv_read_stop((uv_stream_t*)child_ipc_pipe);
    uv_close((uv_handle_t*)child_ipc_pipe, nullptr);
    child_ipc_pipe = nullptr;

    // Call disconnect handlers
    for (TsValue* handler : disconnect_handlers) {
        ts_function_call(handler, 0, nullptr);
    }
}

// Get the IPC channel (for process.channel property)
void* ts_process_get_channel() {
    if (!child_ipc_pipe || !child_ipc_connected) {
        return ts_value_make_null();
    }
    return ts_value_make_object(child_ipc_pipe);
}

// Check if connected to parent
bool ts_process_get_connected() {
    return child_ipc_connected;
}

// Register message handler (for process.on('message', ...))
void ts_process_on_message(void* callback) {
    TsValue* cb = (TsValue*)callback;
    if (cb) {
        message_handlers.push_back(cb);
    }
}

// Register disconnect handler (for process.on('disconnect', ...))
void ts_process_on_disconnect(void* callback) {
    TsValue* cb = (TsValue*)callback;
    if (cb) {
        disconnect_handlers.push_back(cb);
    }
}

// =========================================================================
// Unix User/Group ID functions
// These are Unix-specific; Windows returns -1 or throws
// =========================================================================

int64_t ts_process_getuid() {
#if defined(_WIN32)
    return -1;  // Not supported on Windows
#else
    return (int64_t)getuid();
#endif
}

int64_t ts_process_geteuid() {
#if defined(_WIN32)
    return -1;  // Not supported on Windows
#else
    return (int64_t)geteuid();
#endif
}

int64_t ts_process_getgid() {
#if defined(_WIN32)
    return -1;  // Not supported on Windows
#else
    return (int64_t)getgid();
#endif
}

int64_t ts_process_getegid() {
#if defined(_WIN32)
    return -1;  // Not supported on Windows
#else
    return (int64_t)getegid();
#endif
}

void* ts_process_getgroups() {
#if defined(_WIN32)
    // Return empty array on Windows
    return ts_value_make_object(TsArray::Create());
#else
    int ngroups = getgroups(0, nullptr);
    if (ngroups <= 0) {
        return ts_value_make_object(TsArray::Create());
    }

    gid_t* groups = (gid_t*)ts_alloc(ngroups * sizeof(gid_t));
    ngroups = getgroups(ngroups, groups);

    TsArray* arr = TsArray::Create();
    for (int i = 0; i < ngroups; i++) {
        ts_array_push(arr, ts_value_make_int((int64_t)groups[i]));
    }
    return ts_value_make_object(arr);
#endif
}

void ts_process_setuid(int64_t uid) {
#if defined(_WIN32)
    // Not supported on Windows - throw error
    fprintf(stderr, "process.setuid() is not supported on Windows\n");
#else
    if (setuid((uid_t)uid) != 0) {
        fprintf(stderr, "setuid(%lld) failed: %s\n", (long long)uid, strerror(errno));
    }
#endif
}

void ts_process_seteuid(int64_t uid) {
#if defined(_WIN32)
    fprintf(stderr, "process.seteuid() is not supported on Windows\n");
#else
    if (seteuid((uid_t)uid) != 0) {
        fprintf(stderr, "seteuid(%lld) failed: %s\n", (long long)uid, strerror(errno));
    }
#endif
}

void ts_process_setgid(int64_t gid) {
#if defined(_WIN32)
    fprintf(stderr, "process.setgid() is not supported on Windows\n");
#else
    if (setgid((gid_t)gid) != 0) {
        fprintf(stderr, "setgid(%lld) failed: %s\n", (long long)gid, strerror(errno));
    }
#endif
}

void ts_process_setegid(int64_t gid) {
#if defined(_WIN32)
    fprintf(stderr, "process.setegid() is not supported on Windows\n");
#else
    if (setegid((gid_t)gid) != 0) {
        fprintf(stderr, "setegid(%lld) failed: %s\n", (long long)gid, strerror(errno));
    }
#endif
}

void ts_process_setgroups(void* groups) {
#if defined(_WIN32)
    fprintf(stderr, "process.setgroups() is not supported on Windows\n");
#else
    TsValue* val = (TsValue*)groups;
    void* raw = ts_value_get_object(val);
    if (!raw) raw = groups;

    TsArray* arr = dynamic_cast<TsArray*>((TsObject*)raw);
    if (!arr) {
        fprintf(stderr, "setgroups: expected array argument\n");
        return;
    }

    int64_t len = ts_array_length(arr);
    gid_t* gids = (gid_t*)ts_alloc(len * sizeof(gid_t));

    for (int64_t i = 0; i < len; i++) {
        TsValue* elem = ts_array_get_as_value(arr, i);
        gids[i] = (gid_t)ts_value_get_int(elem);
    }

    if (setgroups((size_t)len, gids) != 0) {
        fprintf(stderr, "setgroups() failed: %s\n", strerror(errno));
    }
#endif
}

void ts_process_initgroups(void* user, int64_t extra_group) {
#if defined(_WIN32)
    fprintf(stderr, "process.initgroups() is not supported on Windows\n");
#else
    TsValue* val = (TsValue*)user;
    void* raw = ts_value_get_string(val);
    if (!raw) raw = user;

    TsString* userStr = (TsString*)raw;
    const char* username = userStr ? userStr->ToUtf8() : "";

    if (initgroups(username, (gid_t)extra_group) != 0) {
        fprintf(stderr, "initgroups(%s, %lld) failed: %s\n", username, (long long)extra_group, strerror(errno));
    }
#endif
}

// =========================================================================
// Stubs for AOT-incompatible features
// =========================================================================

void ts_process_dlopen(void* module, void* filename, void* flags) {
    // dlopen is not supported in AOT compilation
    // Native addons require dynamic loading which is incompatible with AOT
    fprintf(stderr, "process.dlopen() is not supported in AOT-compiled code\n");
}

void ts_process_set_source_maps_enabled(bool enabled) {
    // Source maps are not supported in AOT compilation
    // This is a no-op stub for compatibility
    (void)enabled;
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
        // Debug: try to extract error message
        if (exception) {
            void* raw = ts_value_get_object(exception);
            if (raw) {
                void* msgVal = ts_object_get_property(raw, "message");
                if (msgVal) {
                    fprintf(stderr, "  .message = ");
                    ts_console_log_value((TsValue*)msgVal);
                    fprintf(stderr, "\n");
                }
                void* nameVal = ts_object_get_property(raw, "name");
                if (nameVal) {
                    fprintf(stderr, "  .name = ");
                    ts_console_log_value((TsValue*)nameVal);
                    fprintf(stderr, "\n");
                }
                void* stackVal = ts_object_get_property(raw, "stack");
                if (stackVal) {
                    fprintf(stderr, "  .stack = ");
                    ts_console_log_value((TsValue*)stackVal);
                    fprintf(stderr, "\n");
                }
            }
        }
        fflush(stderr);
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

// The compiler embeds the absolute path to icudt74l.dat (next to ts-aot.exe)
// into each compiled binary via this symbol. The runtime checks it first,
// avoiding the need to copy the 30MB file next to every compiled executable.
// When the symbol isn't provided (e.g., --bundle-icu), the default is empty.
extern "C" const char __ts_icu_data_path[];

// Provide a default empty string so linking succeeds even when the generated
// code doesn't define __ts_icu_data_path (e.g., --bundle-icu mode).
#if defined(_MSC_VER)
    // MSVC: use /alternatename to provide a weak default
    static const char __ts_icu_data_path_default[] = "";
    __pragma(comment(linker, "/alternatename:__ts_icu_data_path=__ts_icu_data_path_default"))
#else
    // GCC/Clang: use weak attribute
    extern "C" __attribute__((weak)) const char __ts_icu_data_path[] = "";
#endif

// Initialize ICU data. When linked with the stub (default, no --bundle-icu),
// the embedded icudt74_dat symbol is a minimal invalid package. We detect this
// by trying u_init(), and if it fails, we search for an external icudt74l.dat
// file and load it directly via udata_setCommonData() (which has the highest
// priority in ICU's data loading order, overriding the compiled-in symbol).
static void ts_icu_init(const char* argv0) {
    UErrorCode status = U_ZERO_ERROR;
    u_init(&status);
    if (U_SUCCESS(status)) {
        return;  // ICU data is embedded (--bundle-icu mode). Done.
    }

    // ICU data not embedded - search for external icudt74l.dat
    const char* datFile = "icudt74l.dat";
    std::filesystem::path exePath;

    if (argv0 && argv0[0]) {
        std::error_code ec;
        exePath = std::filesystem::canonical(argv0, ec);
        if (ec) {
            exePath = std::filesystem::path(argv0);
        }
    }

    // Search locations in priority order
    std::vector<std::filesystem::path> searchPaths;

    // 1. Compiler-embedded path (next to ts-aot.exe, highest priority)
    if (__ts_icu_data_path[0] != '\0') {
        auto embeddedPath = std::filesystem::path(__ts_icu_data_path);
        searchPaths.push_back(embeddedPath.parent_path());
    }

    // 2. Same directory as the executable
    if (!exePath.empty()) {
        searchPaths.push_back(exePath.parent_path());
    }

    // 3. ICU_DATA environment variable
    const char* icuDataEnv = std::getenv("ICU_DATA");
    if (icuDataEnv && icuDataEnv[0]) {
        searchPaths.push_back(std::filesystem::path(icuDataEnv));
    }

    // 4. ../share/icu/ relative to executable (standard install layout)
    if (!exePath.empty()) {
        searchPaths.push_back(exePath.parent_path() / ".." / "share" / "icu");
    }

    // 5. Current working directory
    {
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (!ec) {
            searchPaths.push_back(cwd);
        }
    }

    for (const auto& dir : searchPaths) {
        std::error_code ec;
        auto fullPath = dir / datFile;
        if (!std::filesystem::exists(fullPath, ec)) continue;

        // Read the .dat file into GC-managed memory (persists for program lifetime)
        auto fileSize = std::filesystem::file_size(fullPath, ec);
        if (ec || fileSize < 32) continue;

        FILE* f = fopen(fullPath.string().c_str(), "rb");
        if (!f) continue;

        // Allocate via malloc (not GC!) since ICU holds internal pointers to this data
        // that the GC scanner cannot see. This memory lives for the entire program.
        void* dataBuf = malloc(fileSize);
        size_t bytesRead = fread(dataBuf, 1, fileSize, f);
        fclose(f);
        if (bytesRead != fileSize) continue;

        // Reset ICU state to clear any cached references to the stub data
        u_cleanup();

        // Provide the loaded data directly to ICU. udata_setCommonData() has
        // the highest priority and overrides the compiled-in icudt74_dat symbol.
        status = U_ZERO_ERROR;
        udata_setCommonData(dataBuf, &status);
        if (U_FAILURE(status)) continue;

        status = U_ZERO_ERROR;
        u_init(&status);
        if (U_SUCCESS(status)) {
            return;  // Successfully loaded external ICU data
        }
    }

    // No ICU data found anywhere
    fprintf(stderr,
        "[ts-aot] Error: ICU data file '%s' not found.\n"
        "  Searched:\n", datFile);
    for (const auto& dir : searchPaths) {
        fprintf(stderr, "    - %s\n", dir.string().c_str());
    }
    fprintf(stderr,
        "\n"
        "  Solutions:\n"
        "    1. Place '%s' next to the executable\n"
        "    2. Set ICU_DATA environment variable to the directory containing '%s'\n"
        "    3. Recompile with --bundle-icu to embed ICU data (larger binary)\n",
        datFile, datFile);
    fflush(stderr);
    exit(1);
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

    // 1.1 Register global roots with custom GC
    ts_gc_register_root((void**)&currentException);
    ts_gc_register_root((void**)&process_argv);
    ts_gc_register_root((void**)&process_env);
    ts_gc_register_root((void**)&uncaught_exception_capture_callback);
    ts_gc_register_root((void**)&process_stdout);
    ts_gc_register_root((void**)&process_stderr);
    ts_gc_register_root((void**)&process_stdin);

    // Register scanner for dynamic handler vectors
    ts_gc_register_scanner([](void*) {
        for (auto* v : exit_handlers) ts_gc_mark_object(v);
        for (auto* v : before_exit_handlers) ts_gc_mark_object(v);
        for (auto* v : uncaught_exception_handlers) ts_gc_mark_object(v);
        for (auto* v : warning_handlers) ts_gc_mark_object(v);
        for (auto* v : sigint_handlers) ts_gc_mark_object(v);
        for (auto* v : sigterm_handlers) ts_gc_mark_object(v);
        for (auto* v : message_handlers) ts_gc_mark_object(v);
        for (auto* v : disconnect_handlers) ts_gc_mark_object(v);
    }, nullptr);

    // 1.2 Initialize ICU data (loads external .dat file if not embedded)
    ts_icu_init(argc > 0 ? argv[0] : nullptr);

    // 1.5 Initialize Runtime Globals
    ts_runtime_init();

    // 2. Initialize Event Loop
    ts_loop_init();

    // 2.5 Initialize child IPC if we're a forked process
    ts_process_init_child_ipc();

    // 2.6 Initialize Node.js modules via hook (cluster, etc.)
    if (ts_node_init_hook) ts_node_init_hook();

    // 3. Initialize process.argv (Node.js layout: [exePath, scriptPath, ...userArgs])
    TsArray* argvArray = TsArray::Create(argc + 1);
    // argv[0] = exe path (equivalent to "node" in Node.js)
    void* exeStr = ts_value_make_string(ts_string_create(argv[0]));
    argvArray->Push((int64_t)exeStr);
    // argv[1] = exe path again (equivalent to "script.js" in Node.js)
    argvArray->Push((int64_t)exeStr);
    // argv[2..n] = user arguments
    for (int i = 1; i < argc; ++i) {
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

    // 5. Run Event Loop (wrapped in exception handler for cleanup errors)
    {
        ExceptionContext* evCtx = (ExceptionContext*)malloc(sizeof(ExceptionContext));
        exceptionStack.push_back(evCtx);
        if (setjmp(evCtx->env) == 0) {
            ts_loop_run();
            // Normal completion - pop the handler
            if (!exceptionStack.empty()) {
                exceptionStack.pop_back();
                free(evCtx);
            }
        } else {
            // Exception caught during event loop - swallow cleanup errors
            // (e.g. connection errors during session.destroy())
        }
    }

    return 0;
}

}
