#pragma once

#include "TsString.h"
#include "TsObject.h"

extern "C" {
    // OS info functions
    TsString* ts_os_platform();
    TsString* ts_os_arch();
    TsString* ts_os_type();
    TsString* ts_os_release();
    TsString* ts_os_version();
    TsString* ts_os_hostname();
    TsString* ts_os_machine();

    // Path functions
    TsString* ts_os_homedir();
    TsString* ts_os_tmpdir();

    // Constants
    TsString* ts_os_get_eol();
    TsString* ts_os_get_devnull();

    // Memory functions
    int64_t ts_os_totalmem();
    int64_t ts_os_freemem();

    // Other info functions
    double ts_os_uptime();
    TsValue* ts_os_loadavg();
    TsValue* ts_os_cpus();
    TsString* ts_os_endianness();
    TsValue* ts_os_networkInterfaces();
    TsValue* ts_os_userInfo();

    // Priority functions
    int64_t ts_os_getPriority(int64_t pid);
    void ts_os_setPriority(int64_t pid, int64_t priority);
}
