// TsOs.h - OS module for ts-aot
//
// Node.js-compatible operating system information functions.
// This module is built as a separate library (ts_os) and linked
// only when the os module is imported.

#ifndef TS_OS_EXT_H
#define TS_OS_EXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OS info functions
void* ts_os_platform();
void* ts_os_arch();
void* ts_os_type();
void* ts_os_release();
void* ts_os_version();
void* ts_os_hostname();
void* ts_os_machine();

// Path functions
void* ts_os_homedir();
void* ts_os_tmpdir();

// Constants (strings)
void* ts_os_get_eol();
void* ts_os_get_devnull();

// Memory functions
int64_t ts_os_totalmem();
int64_t ts_os_freemem();
int64_t ts_os_availableParallelism();

// Other info functions
double ts_os_uptime();
void* ts_os_loadavg();
void* ts_os_cpus();
void* ts_os_endianness();
void* ts_os_networkInterfaces();
void* ts_os_userInfo();

// Priority functions
int64_t ts_os_getPriority(int64_t pid);
void ts_os_setPriority(int64_t pid, int64_t priority);

// Constants (objects)
void* ts_os_get_constants();
void* ts_os_get_signals();
void* ts_os_get_errno();
void* ts_os_get_priority_constants();

#ifdef __cplusplus
}
#endif

#endif // TS_OS_EXT_H
