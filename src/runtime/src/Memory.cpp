#include "GC.h"
#include "TsGC.h"
#include "StackMap.h"
#include "GCRoots.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>

// Custom assertion handler to print to stderr instead of GUI popup
int CustomReportHook(int reportType, char* message, int* returnValue) {
    fprintf(stderr, "[ASSERT] %s\n", message);
    fflush(stderr);
    *returnValue = 0; // Don't break
    return TRUE; // We handled it
}
#endif

extern "C" {

// Main allocation function - delegates to custom GC
void* ts_alloc(size_t size) {
    return ts_gc_alloc(size);
}

// Explicit pool allocation (same as ts_alloc)
void* ts_pool_alloc(size_t size) {
    return ts_gc_alloc(size);
}

void ts_gc_init() {
#ifdef _WIN32
    // Disable Windows error dialogs - send to stderr instead
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // Redirect CRT assertions to stderr
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);

    // Set custom report hook
    _CrtSetReportHook(CustomReportHook);

    fflush(stderr);
#endif

    // Initialize custom GC (creates GCHeap, checks TS_GC_VERBOSE)
    // The first ts_gc_alloc() call also initializes if needed, but
    // we call it explicitly here so stack maps and roots are set up early.
    ts_gc_alloc(0);  // Force init; returns 8-byte minimum allocation

    // Parse LLVM stack maps if present (from --gc-statepoints builds)
    ts_stackmap_init();

    // Initialize precise root pushing (uses parsed stack maps)
    ts_gc_roots_init();
}

void ts_gc_collect() {
    ts_gc_force_collect();
}

void __stack_chk_fail() {
    fprintf(stderr, "Stack smashing detected!\n");
    abort();
}

void ts_cfi_fail() {
    fprintf(stderr, "Control Flow Integrity check failed!\n");
    abort();
}

void ts_panic(const char* msg) {
    fprintf(stderr, "Runtime Panic: %s\n", msg);
    abort();
}

}
