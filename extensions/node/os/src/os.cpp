// os.cpp - OS module implementation for ts-aot
//
// Node.js-compatible operating system information functions.

#include "TsOs.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsNanBox.h"
#include "TsRuntime.h"
#include "GC.h"
#include <uv.h>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <lmcons.h>
#else
#include <sys/utsname.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pwd.h>
#endif

extern "C" {

void* ts_os_platform() {
#ifdef _WIN32
    return TsString::Create("win32");
#elif defined(__APPLE__)
    return TsString::Create("darwin");
#elif defined(__linux__)
    return TsString::Create("linux");
#elif defined(__FreeBSD__)
    return TsString::Create("freebsd");
#else
    return TsString::Create("unknown");
#endif
}

void* ts_os_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return TsString::Create("x64");
#elif defined(__i386__) || defined(_M_IX86)
    return TsString::Create("ia32");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return TsString::Create("arm64");
#elif defined(__arm__) || defined(_M_ARM)
    return TsString::Create("arm");
#else
    return TsString::Create("unknown");
#endif
}

void* ts_os_type() {
#ifdef _WIN32
    return TsString::Create("Windows_NT");
#elif defined(__APPLE__)
    return TsString::Create("Darwin");
#elif defined(__linux__)
    return TsString::Create("Linux");
#elif defined(__FreeBSD__)
    return TsString::Create("FreeBSD");
#else
    return TsString::Create("Unknown");
#endif
}

void* ts_os_release() {
#ifdef _WIN32
    // Get Windows version info
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    // Note: GetVersionEx is deprecated but still works
    #pragma warning(push)
    #pragma warning(disable: 4996)
    if (GetVersionEx((OSVERSIONINFO*)&osvi)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%ld.%ld.%ld", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
        return TsString::Create(buf);
    }
    #pragma warning(pop)
    return TsString::Create("unknown");
#else
    struct utsname uts;
    if (uname(&uts) == 0) {
        return TsString::Create(uts.release);
    }
    return TsString::Create("unknown");
#endif
}

void* ts_os_version() {
#ifdef _WIN32
    return ts_os_release();  // Same as release on Windows
#else
    struct utsname uts;
    if (uname(&uts) == 0) {
        return TsString::Create(uts.version);
    }
    return TsString::Create("unknown");
#endif
}

void* ts_os_hostname() {
    char hostname[256];
    size_t size = sizeof(hostname);
    if (uv_os_gethostname(hostname, &size) == 0) {
        return TsString::Create(hostname);
    }
    return TsString::Create("localhost");
}

void* ts_os_machine() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return TsString::Create("x86_64");
        case PROCESSOR_ARCHITECTURE_ARM64: return TsString::Create("aarch64");
        case PROCESSOR_ARCHITECTURE_INTEL: return TsString::Create("i686");
        case PROCESSOR_ARCHITECTURE_ARM: return TsString::Create("arm");
        default: return TsString::Create("unknown");
    }
#else
    struct utsname uts;
    if (uname(&uts) == 0) {
        return TsString::Create(uts.machine);
    }
    return TsString::Create("unknown");
#endif
}

void* ts_os_homedir() {
    char buf[1024];
    size_t size = sizeof(buf);
    if (uv_os_homedir(buf, &size) == 0) {
        return TsString::Create(buf);
    }

    // Fallback
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
#else
    const char* home = getenv("HOME");
#endif
    if (home) {
        return TsString::Create(home);
    }
    return TsString::Create("");
}

void* ts_os_tmpdir() {
    char buf[1024];
    size_t size = sizeof(buf);
    if (uv_os_tmpdir(buf, &size) == 0) {
        return TsString::Create(buf);
    }

    // Fallback
#ifdef _WIN32
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (tmp) return TsString::Create(tmp);
    return TsString::Create("C:\\Windows\\Temp");
#else
    const char* tmp = getenv("TMPDIR");
    if (tmp) return TsString::Create(tmp);
    return TsString::Create("/tmp");
#endif
}

void* ts_os_get_eol() {
#ifdef _WIN32
    return TsString::Create("\r\n");
#else
    return TsString::Create("\n");
#endif
}

void* ts_os_get_devnull() {
#ifdef _WIN32
    return TsString::Create("\\\\.\\nul");
#else
    return TsString::Create("/dev/null");
#endif
}

int64_t ts_os_totalmem() {
    return (int64_t)uv_get_total_memory();
}

int64_t ts_os_freemem() {
    return (int64_t)uv_get_free_memory();
}

int64_t ts_os_availableParallelism() {
    // Returns the number of logical CPUs available to the process
    // Same as os.cpus().length but more efficient
    uv_cpu_info_t* cpus;
    int count;
    if (uv_cpu_info(&cpus, &count) != 0) {
        return 1;  // Fallback to 1 if we can't determine
    }
    uv_free_cpu_info(cpus, count);
    return (int64_t)count;
}

double ts_os_uptime() {
    double uptime;
    if (uv_uptime(&uptime) == 0) {
        return uptime;
    }
    return 0.0;
}

void* ts_os_loadavg() {
    double avg[3];
    uv_loadavg(avg);

    // Create array of 3 doubles
    TsArray* arr = TsArray::Create();
    for (int i = 0; i < 3; i++) {
        ts_array_push(arr, ts_value_make_double(avg[i]));
    }
    return ts_value_make_object(arr);
}

void* ts_os_cpus() {
    uv_cpu_info_t* cpus;
    int count;

    if (uv_cpu_info(&cpus, &count) != 0) {
        return ts_value_make_object(TsArray::Create());
    }

    TsArray* arr = TsArray::Create();
    for (int i = 0; i < count; i++) {
        TsMap* cpu = TsMap::Create();
        cpu->Set(TsString::Create("model"), nanbox_to_tagged(ts_value_make_string(TsString::Create(cpus[i].model))));
        cpu->Set(TsString::Create("speed"), nanbox_to_tagged(ts_value_make_int(cpus[i].speed)));

        TsMap* times = TsMap::Create();
        times->Set(TsString::Create("user"), nanbox_to_tagged(ts_value_make_int(cpus[i].cpu_times.user)));
        times->Set(TsString::Create("nice"), nanbox_to_tagged(ts_value_make_int(cpus[i].cpu_times.nice)));
        times->Set(TsString::Create("sys"), nanbox_to_tagged(ts_value_make_int(cpus[i].cpu_times.sys)));
        times->Set(TsString::Create("idle"), nanbox_to_tagged(ts_value_make_int(cpus[i].cpu_times.idle)));
        times->Set(TsString::Create("irq"), nanbox_to_tagged(ts_value_make_int(cpus[i].cpu_times.irq)));
        cpu->Set(TsString::Create("times"), nanbox_to_tagged(ts_value_make_object(times)));

        ts_array_push(arr, ts_value_make_object(cpu));
    }

    uv_free_cpu_info(cpus, count);
    return ts_value_make_object(arr);
}

void* ts_os_endianness() {
    uint16_t test = 1;
    return TsString::Create(*(uint8_t*)&test == 1 ? "LE" : "BE");
}

void* ts_os_networkInterfaces() {
    uv_interface_address_t* addresses;
    int count;

    if (uv_interface_addresses(&addresses, &count) != 0) {
        return ts_value_make_object(TsMap::Create());
    }

    TsMap* result = TsMap::Create();

    for (int i = 0; i < count; i++) {
        TsString* name = TsString::Create(addresses[i].name);

        TsMap* info = TsMap::Create();

        char ip[46];  // IPv6 max length
        if (addresses[i].address.address4.sin_family == AF_INET) {
            uv_ip4_name(&addresses[i].address.address4, ip, sizeof(ip));
            info->Set(TsString::Create("family"), nanbox_to_tagged(ts_value_make_string(TsString::Create("IPv4"))));
        } else {
            uv_ip6_name(&addresses[i].address.address6, ip, sizeof(ip));
            info->Set(TsString::Create("family"), nanbox_to_tagged(ts_value_make_string(TsString::Create("IPv6"))));
        }
        info->Set(TsString::Create("address"), nanbox_to_tagged(ts_value_make_string(TsString::Create(ip))));

        char mac[18];
        char* phys = addresses[i].phys_addr;
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 phys[0], phys[1], phys[2], phys[3], phys[4], phys[5]);
        info->Set(TsString::Create("mac"), nanbox_to_tagged(ts_value_make_string(TsString::Create(mac))));
        info->Set(TsString::Create("internal"), nanbox_to_tagged(ts_value_make_bool(addresses[i].is_internal)));

        // Get or create array for this interface name
        TsValue existing = result->Get(name);
        TsArray* ifaceArr;
        if (existing.type != ValueType::UNDEFINED) {
            // Direct field access — existing is a TsValue struct, not a NaN-boxed pointer
            ifaceArr = (TsArray*)existing.ptr_val;
        } else {
            ifaceArr = TsArray::Create();
            result->Set(name, nanbox_to_tagged(ts_value_make_object(ifaceArr)));
        }
        ts_array_push(ifaceArr, ts_value_make_object(info));
    }

    uv_free_interface_addresses(addresses, count);
    return ts_value_make_object(result);
}

void* ts_os_userInfo() {
    TsMap* info = TsMap::Create();

    uv_passwd_t pwd;
    if (uv_os_get_passwd(&pwd) == 0) {
        info->Set(TsString::Create("uid"), nanbox_to_tagged(ts_value_make_int(pwd.uid)));
        info->Set(TsString::Create("gid"), nanbox_to_tagged(ts_value_make_int(pwd.gid)));
        info->Set(TsString::Create("username"), nanbox_to_tagged(ts_value_make_string(TsString::Create(pwd.username))));
        info->Set(TsString::Create("homedir"), nanbox_to_tagged(ts_value_make_string(TsString::Create(pwd.homedir))));
        info->Set(TsString::Create("shell"), nanbox_to_tagged(ts_value_make_string(TsString::Create(pwd.shell ? pwd.shell : ""))));
        uv_os_free_passwd(&pwd);
    } else {
#ifdef _WIN32
        char username[UNLEN + 1];
        DWORD size = sizeof(username);
        if (GetUserNameA(username, &size)) {
            info->Set(TsString::Create("username"), nanbox_to_tagged(ts_value_make_string(TsString::Create(username))));
        }
        info->Set(TsString::Create("uid"), nanbox_to_tagged(ts_value_make_int(-1)));
        info->Set(TsString::Create("gid"), nanbox_to_tagged(ts_value_make_int(-1)));
#else
        info->Set(TsString::Create("uid"), nanbox_to_tagged(ts_value_make_int(getuid())));
        info->Set(TsString::Create("gid"), nanbox_to_tagged(ts_value_make_int(getgid())));

        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            info->Set(TsString::Create("username"), nanbox_to_tagged(ts_value_make_string(TsString::Create(pw->pw_name))));
            info->Set(TsString::Create("homedir"), nanbox_to_tagged(ts_value_make_string(TsString::Create(pw->pw_dir))));
            info->Set(TsString::Create("shell"), nanbox_to_tagged(ts_value_make_string(TsString::Create(pw->pw_shell))));
        }
#endif
    }

    return ts_value_make_object(info);
}

int64_t ts_os_getPriority(int64_t pid) {
#ifdef _WIN32
    HANDLE hProcess = pid == 0 ? GetCurrentProcess() : OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    if (hProcess) {
        DWORD priority = GetPriorityClass(hProcess);
        if (pid != 0) CloseHandle(hProcess);

        // Convert Windows priority to nice-like value
        switch (priority) {
            case REALTIME_PRIORITY_CLASS: return -20;
            case HIGH_PRIORITY_CLASS: return -15;
            case ABOVE_NORMAL_PRIORITY_CLASS: return -5;
            case NORMAL_PRIORITY_CLASS: return 0;
            case BELOW_NORMAL_PRIORITY_CLASS: return 10;
            case IDLE_PRIORITY_CLASS: return 19;
            default: return 0;
        }
    }
    return 0;
#else
    return (int64_t)getpriority(PRIO_PROCESS, (id_t)pid);
#endif
}

void ts_os_setPriority(int64_t pid, int64_t priority) {
#ifdef _WIN32
    HANDLE hProcess = pid == 0 ? GetCurrentProcess() : OpenProcess(PROCESS_SET_INFORMATION, FALSE, (DWORD)pid);
    if (hProcess) {
        DWORD winPriority;
        if (priority <= -15) winPriority = HIGH_PRIORITY_CLASS;
        else if (priority <= -5) winPriority = ABOVE_NORMAL_PRIORITY_CLASS;
        else if (priority <= 5) winPriority = NORMAL_PRIORITY_CLASS;
        else if (priority <= 15) winPriority = BELOW_NORMAL_PRIORITY_CLASS;
        else winPriority = IDLE_PRIORITY_CLASS;

        SetPriorityClass(hProcess, winPriority);
        if (pid != 0) CloseHandle(hProcess);
    }
#else
    setpriority(PRIO_PROCESS, (id_t)pid, (int)priority);
#endif
}

// ========================================================================
// Constants
// ========================================================================

void* ts_os_get_signals() {
    TsMap* signals = TsMap::Create();

    // Common POSIX signals
    signals->Set(TsString::Create("SIGHUP"), nanbox_to_tagged(ts_value_make_int(1)));
    signals->Set(TsString::Create("SIGINT"), nanbox_to_tagged(ts_value_make_int(2)));
    signals->Set(TsString::Create("SIGQUIT"), nanbox_to_tagged(ts_value_make_int(3)));
    signals->Set(TsString::Create("SIGILL"), nanbox_to_tagged(ts_value_make_int(4)));
    signals->Set(TsString::Create("SIGTRAP"), nanbox_to_tagged(ts_value_make_int(5)));
    signals->Set(TsString::Create("SIGABRT"), nanbox_to_tagged(ts_value_make_int(6)));
    signals->Set(TsString::Create("SIGFPE"), nanbox_to_tagged(ts_value_make_int(8)));
    signals->Set(TsString::Create("SIGKILL"), nanbox_to_tagged(ts_value_make_int(9)));
    signals->Set(TsString::Create("SIGSEGV"), nanbox_to_tagged(ts_value_make_int(11)));
    signals->Set(TsString::Create("SIGPIPE"), nanbox_to_tagged(ts_value_make_int(13)));
    signals->Set(TsString::Create("SIGALRM"), nanbox_to_tagged(ts_value_make_int(14)));
    signals->Set(TsString::Create("SIGTERM"), nanbox_to_tagged(ts_value_make_int(15)));
    signals->Set(TsString::Create("SIGCHLD"), nanbox_to_tagged(ts_value_make_int(17)));
    signals->Set(TsString::Create("SIGCONT"), nanbox_to_tagged(ts_value_make_int(18)));
    signals->Set(TsString::Create("SIGSTOP"), nanbox_to_tagged(ts_value_make_int(19)));
    signals->Set(TsString::Create("SIGTSTP"), nanbox_to_tagged(ts_value_make_int(20)));
    signals->Set(TsString::Create("SIGTTIN"), nanbox_to_tagged(ts_value_make_int(21)));
    signals->Set(TsString::Create("SIGTTOU"), nanbox_to_tagged(ts_value_make_int(22)));
    signals->Set(TsString::Create("SIGURG"), nanbox_to_tagged(ts_value_make_int(23)));
    signals->Set(TsString::Create("SIGXCPU"), nanbox_to_tagged(ts_value_make_int(24)));
    signals->Set(TsString::Create("SIGXFSZ"), nanbox_to_tagged(ts_value_make_int(25)));
    signals->Set(TsString::Create("SIGVTALRM"), nanbox_to_tagged(ts_value_make_int(26)));
    signals->Set(TsString::Create("SIGPROF"), nanbox_to_tagged(ts_value_make_int(27)));
    signals->Set(TsString::Create("SIGWINCH"), nanbox_to_tagged(ts_value_make_int(28)));
    signals->Set(TsString::Create("SIGIO"), nanbox_to_tagged(ts_value_make_int(29)));
    signals->Set(TsString::Create("SIGUSR1"), nanbox_to_tagged(ts_value_make_int(10)));
    signals->Set(TsString::Create("SIGUSR2"), nanbox_to_tagged(ts_value_make_int(12)));

    return ts_value_make_object(signals);
}

void* ts_os_get_errno() {
    TsMap* errnoMap = TsMap::Create();

    // Common POSIX error codes
    errnoMap->Set(TsString::Create("EPERM"), nanbox_to_tagged(ts_value_make_int(1)));
    errnoMap->Set(TsString::Create("ENOENT"), nanbox_to_tagged(ts_value_make_int(2)));
    errnoMap->Set(TsString::Create("ESRCH"), nanbox_to_tagged(ts_value_make_int(3)));
    errnoMap->Set(TsString::Create("EINTR"), nanbox_to_tagged(ts_value_make_int(4)));
    errnoMap->Set(TsString::Create("EIO"), nanbox_to_tagged(ts_value_make_int(5)));
    errnoMap->Set(TsString::Create("ENXIO"), nanbox_to_tagged(ts_value_make_int(6)));
    errnoMap->Set(TsString::Create("E2BIG"), nanbox_to_tagged(ts_value_make_int(7)));
    errnoMap->Set(TsString::Create("ENOEXEC"), nanbox_to_tagged(ts_value_make_int(8)));
    errnoMap->Set(TsString::Create("EBADF"), nanbox_to_tagged(ts_value_make_int(9)));
    errnoMap->Set(TsString::Create("ECHILD"), nanbox_to_tagged(ts_value_make_int(10)));
    errnoMap->Set(TsString::Create("EAGAIN"), nanbox_to_tagged(ts_value_make_int(11)));
    errnoMap->Set(TsString::Create("ENOMEM"), nanbox_to_tagged(ts_value_make_int(12)));
    errnoMap->Set(TsString::Create("EACCES"), nanbox_to_tagged(ts_value_make_int(13)));
    errnoMap->Set(TsString::Create("EFAULT"), nanbox_to_tagged(ts_value_make_int(14)));
    errnoMap->Set(TsString::Create("EBUSY"), nanbox_to_tagged(ts_value_make_int(16)));
    errnoMap->Set(TsString::Create("EEXIST"), nanbox_to_tagged(ts_value_make_int(17)));
    errnoMap->Set(TsString::Create("EXDEV"), nanbox_to_tagged(ts_value_make_int(18)));
    errnoMap->Set(TsString::Create("ENODEV"), nanbox_to_tagged(ts_value_make_int(19)));
    errnoMap->Set(TsString::Create("ENOTDIR"), nanbox_to_tagged(ts_value_make_int(20)));
    errnoMap->Set(TsString::Create("EISDIR"), nanbox_to_tagged(ts_value_make_int(21)));
    errnoMap->Set(TsString::Create("EINVAL"), nanbox_to_tagged(ts_value_make_int(22)));
    errnoMap->Set(TsString::Create("ENFILE"), nanbox_to_tagged(ts_value_make_int(23)));
    errnoMap->Set(TsString::Create("EMFILE"), nanbox_to_tagged(ts_value_make_int(24)));
    errnoMap->Set(TsString::Create("ENOTTY"), nanbox_to_tagged(ts_value_make_int(25)));
    errnoMap->Set(TsString::Create("EFBIG"), nanbox_to_tagged(ts_value_make_int(27)));
    errnoMap->Set(TsString::Create("ENOSPC"), nanbox_to_tagged(ts_value_make_int(28)));
    errnoMap->Set(TsString::Create("ESPIPE"), nanbox_to_tagged(ts_value_make_int(29)));
    errnoMap->Set(TsString::Create("EROFS"), nanbox_to_tagged(ts_value_make_int(30)));
    errnoMap->Set(TsString::Create("EMLINK"), nanbox_to_tagged(ts_value_make_int(31)));
    errnoMap->Set(TsString::Create("EPIPE"), nanbox_to_tagged(ts_value_make_int(32)));
    errnoMap->Set(TsString::Create("EDOM"), nanbox_to_tagged(ts_value_make_int(33)));
    errnoMap->Set(TsString::Create("ERANGE"), nanbox_to_tagged(ts_value_make_int(34)));
    errnoMap->Set(TsString::Create("EDEADLK"), nanbox_to_tagged(ts_value_make_int(35)));
    errnoMap->Set(TsString::Create("ENAMETOOLONG"), nanbox_to_tagged(ts_value_make_int(36)));
    errnoMap->Set(TsString::Create("ENOLCK"), nanbox_to_tagged(ts_value_make_int(37)));
    errnoMap->Set(TsString::Create("ENOSYS"), nanbox_to_tagged(ts_value_make_int(38)));
    errnoMap->Set(TsString::Create("ENOTEMPTY"), nanbox_to_tagged(ts_value_make_int(39)));
    errnoMap->Set(TsString::Create("ELOOP"), nanbox_to_tagged(ts_value_make_int(40)));
    errnoMap->Set(TsString::Create("EWOULDBLOCK"), nanbox_to_tagged(ts_value_make_int(11)));  // Same as EAGAIN

    // Network errors
    errnoMap->Set(TsString::Create("ECONNREFUSED"), nanbox_to_tagged(ts_value_make_int(111)));
    errnoMap->Set(TsString::Create("ECONNRESET"), nanbox_to_tagged(ts_value_make_int(104)));
    errnoMap->Set(TsString::Create("ECONNABORTED"), nanbox_to_tagged(ts_value_make_int(103)));
    errnoMap->Set(TsString::Create("ETIMEDOUT"), nanbox_to_tagged(ts_value_make_int(110)));
    errnoMap->Set(TsString::Create("ENETUNREACH"), nanbox_to_tagged(ts_value_make_int(101)));
    errnoMap->Set(TsString::Create("EHOSTUNREACH"), nanbox_to_tagged(ts_value_make_int(113)));
    errnoMap->Set(TsString::Create("EADDRINUSE"), nanbox_to_tagged(ts_value_make_int(98)));
    errnoMap->Set(TsString::Create("EADDRNOTAVAIL"), nanbox_to_tagged(ts_value_make_int(99)));

    return ts_value_make_object(errnoMap);
}

void* ts_os_get_priority_constants() {
    TsMap* priority = TsMap::Create();

    // Process priority constants (matching Node.js)
    priority->Set(TsString::Create("PRIORITY_LOW"), nanbox_to_tagged(ts_value_make_int(19)));
    priority->Set(TsString::Create("PRIORITY_BELOW_NORMAL"), nanbox_to_tagged(ts_value_make_int(10)));
    priority->Set(TsString::Create("PRIORITY_NORMAL"), nanbox_to_tagged(ts_value_make_int(0)));
    priority->Set(TsString::Create("PRIORITY_ABOVE_NORMAL"), nanbox_to_tagged(ts_value_make_int(-7)));
    priority->Set(TsString::Create("PRIORITY_HIGH"), nanbox_to_tagged(ts_value_make_int(-14)));
    priority->Set(TsString::Create("PRIORITY_HIGHEST"), nanbox_to_tagged(ts_value_make_int(-20)));

    return ts_value_make_object(priority);
}

void* ts_os_get_constants() {
    TsMap* constants = TsMap::Create();

    // Get signals
    TsValue* signals = (TsValue*)ts_os_get_signals();
    constants->Set(TsString::Create("signals"), nanbox_to_tagged(signals));

    // Get errno
    TsValue* errnoVal = (TsValue*)ts_os_get_errno();
    constants->Set(TsString::Create("errno"), nanbox_to_tagged(errnoVal));

    // Get priority
    TsValue* priority = (TsValue*)ts_os_get_priority_constants();
    constants->Set(TsString::Create("priority"), nanbox_to_tagged(priority));

    return ts_value_make_object(constants);
}

} // extern "C"

// Register os functions for create_builtin_module("os")
static struct OsRegistrar {
    OsRegistrar() {
        ts_builtin_register("os", "homedir", (void*)ts_os_homedir, TS_THUNK_FN);
        ts_builtin_register("os", "tmpdir", (void*)ts_os_tmpdir, TS_THUNK_FN);
        ts_builtin_register("os", "platform", (void*)ts_os_platform, TS_THUNK_FN);
        ts_builtin_register("os", "type", (void*)ts_os_type, TS_THUNK_FN);
        ts_builtin_register("os", "hostname", (void*)ts_os_hostname, TS_THUNK_FN);
#ifdef _WIN32
        ts_builtin_register_str_prop("os", "EOL", "\r\n");
#else
        ts_builtin_register_str_prop("os", "EOL", "\n");
#endif
    }
} g_os_registrar;
