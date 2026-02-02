// VM extension for ts-aot
// Stub implementation for vm module - not compatible with AOT compilation.
// These stubs allow code that imports 'vm' to compile, then throw an error at runtime.

#include "TsVmExt.h"
#include "TsString.h"
#include "TsObject.h"
#include <cstdio>
#include <cstdlib>

static void vm_not_supported_error(const char* method) {
    fprintf(stderr, "Error: vm.%s() is not supported in AOT-compiled code.\n", method);
    fprintf(stderr, "The vm module requires runtime code evaluation which is incompatible with ahead-of-time compilation.\n");
    exit(1);
}

extern "C" {

// All vm module methods call this stub which throws an error
void* ts_vm_not_supported(void* methodName) {
    TsString* name = (TsString*)methodName;
    const char* nameStr = name ? name->ToUtf8() : "unknown";
    vm_not_supported_error(nameStr);
    return nullptr;
}

// Script class method stubs - these are needed for vtable generation
// Script.runInContext(contextifiedObject, options?) -> any
void* Script_runInContext(void* self, void* context, void* options) {
    (void)self;
    (void)context;
    (void)options;
    vm_not_supported_error("Script.runInContext");
    return nullptr;
}

// Script.runInNewContext(contextObject?, options?) -> any
void* Script_runInNewContext(void* self, void* context, void* options) {
    (void)self;
    (void)context;
    (void)options;
    vm_not_supported_error("Script.runInNewContext");
    return nullptr;
}

// Script.runInThisContext(options?) -> any
void* Script_runInThisContext(void* self, void* options) {
    (void)self;
    (void)options;
    vm_not_supported_error("Script.runInThisContext");
    return nullptr;
}

// Script.createCachedData() -> Buffer
void* Script_createCachedData(void* self) {
    (void)self;
    vm_not_supported_error("Script.createCachedData");
    return nullptr;
}

} // extern "C"
