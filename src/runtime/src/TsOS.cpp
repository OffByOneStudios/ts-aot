#include "TsOS.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsMap.h"
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

TsString* ts_os_platform() {
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

TsString* ts_os_arch() {
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

TsString* ts_os_type() {
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

TsString* ts_os_release() {
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

TsString* ts_os_version() {
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

TsString* ts_os_hostname() {
    char hostname[256];
    size_t size = sizeof(hostname);
    if (uv_os_gethostname(hostname, &size) == 0) {
        return TsString::Create(hostname);
    }
    return TsString::Create("localhost");
}

TsString* ts_os_machine() {
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

TsString* ts_os_homedir() {
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

TsString* ts_os_tmpdir() {
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

TsString* ts_os_get_eol() {
#ifdef _WIN32
    return TsString::Create("\r\n");
#else
    return TsString::Create("\n");
#endif
}

TsString* ts_os_get_devnull() {
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

double ts_os_uptime() {
    double uptime;
    if (uv_uptime(&uptime) == 0) {
        return uptime;
    }
    return 0.0;
}

TsValue* ts_os_loadavg() {
    double avg[3];
    uv_loadavg(avg);

    // Create array of 3 doubles
    TsArray* arr = TsArray::Create();
    for (int i = 0; i < 3; i++) {
        ts_array_push(arr, ts_value_make_double(avg[i]));
    }
    return ts_value_make_object(arr);
}

TsValue* ts_os_cpus() {
    uv_cpu_info_t* cpus;
    int count;

    if (uv_cpu_info(&cpus, &count) != 0) {
        return ts_value_make_object(TsArray::Create());
    }

    TsArray* arr = TsArray::Create();
    for (int i = 0; i < count; i++) {
        TsMap* cpu = TsMap::Create();
        cpu->Set(TsString::Create("model"), *ts_value_make_string(TsString::Create(cpus[i].model)));
        cpu->Set(TsString::Create("speed"), *ts_value_make_int(cpus[i].speed));

        TsMap* times = TsMap::Create();
        times->Set(TsString::Create("user"), *ts_value_make_int(cpus[i].cpu_times.user));
        times->Set(TsString::Create("nice"), *ts_value_make_int(cpus[i].cpu_times.nice));
        times->Set(TsString::Create("sys"), *ts_value_make_int(cpus[i].cpu_times.sys));
        times->Set(TsString::Create("idle"), *ts_value_make_int(cpus[i].cpu_times.idle));
        times->Set(TsString::Create("irq"), *ts_value_make_int(cpus[i].cpu_times.irq));
        cpu->Set(TsString::Create("times"), *ts_value_make_object(times));

        ts_array_push(arr, ts_value_make_object(cpu));
    }

    uv_free_cpu_info(cpus, count);
    return ts_value_make_object(arr);
}

TsString* ts_os_endianness() {
    uint16_t test = 1;
    return TsString::Create(*(uint8_t*)&test == 1 ? "LE" : "BE");
}

TsValue* ts_os_networkInterfaces() {
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
            info->Set(TsString::Create("family"), *ts_value_make_string(TsString::Create("IPv4")));
        } else {
            uv_ip6_name(&addresses[i].address.address6, ip, sizeof(ip));
            info->Set(TsString::Create("family"), *ts_value_make_string(TsString::Create("IPv6")));
        }
        info->Set(TsString::Create("address"), *ts_value_make_string(TsString::Create(ip)));

        char mac[18];
        char* phys = addresses[i].phys_addr;
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 phys[0], phys[1], phys[2], phys[3], phys[4], phys[5]);
        info->Set(TsString::Create("mac"), *ts_value_make_string(TsString::Create(mac)));
        info->Set(TsString::Create("internal"), *ts_value_make_bool(addresses[i].is_internal));

        // Get or create array for this interface name
        TsValue existing = result->Get(name);
        TsArray* ifaceArr;
        if (existing.type != ValueType::UNDEFINED) {
            ifaceArr = (TsArray*)ts_value_get_object(&existing);
        } else {
            ifaceArr = TsArray::Create();
            result->Set(name, *ts_value_make_object(ifaceArr));
        }
        ts_array_push(ifaceArr, ts_value_make_object(info));
    }

    uv_free_interface_addresses(addresses, count);
    return ts_value_make_object(result);
}

TsValue* ts_os_userInfo() {
    TsMap* info = TsMap::Create();

    uv_passwd_t pwd;
    if (uv_os_get_passwd(&pwd) == 0) {
        info->Set(TsString::Create("uid"), *ts_value_make_int(pwd.uid));
        info->Set(TsString::Create("gid"), *ts_value_make_int(pwd.gid));
        info->Set(TsString::Create("username"), *ts_value_make_string(TsString::Create(pwd.username)));
        info->Set(TsString::Create("homedir"), *ts_value_make_string(TsString::Create(pwd.homedir)));
        info->Set(TsString::Create("shell"), *ts_value_make_string(TsString::Create(pwd.shell ? pwd.shell : "")));
        uv_os_free_passwd(&pwd);
    } else {
#ifdef _WIN32
        char username[UNLEN + 1];
        DWORD size = sizeof(username);
        if (GetUserNameA(username, &size)) {
            info->Set(TsString::Create("username"), *ts_value_make_string(TsString::Create(username)));
        }
        info->Set(TsString::Create("uid"), *ts_value_make_int(-1));
        info->Set(TsString::Create("gid"), *ts_value_make_int(-1));
#else
        info->Set(TsString::Create("uid"), *ts_value_make_int(getuid()));
        info->Set(TsString::Create("gid"), *ts_value_make_int(getgid()));

        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            info->Set(TsString::Create("username"), *ts_value_make_string(TsString::Create(pw->pw_name)));
            info->Set(TsString::Create("homedir"), *ts_value_make_string(TsString::Create(pw->pw_dir)));
            info->Set(TsString::Create("shell"), *ts_value_make_string(TsString::Create(pw->pw_shell)));
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

} // extern "C"
