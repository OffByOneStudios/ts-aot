// TsVM.cpp - Stub implementation for vm module
// The vm module is incompatible with AOT compilation as it requires runtime code evaluation.

#include "TsString.h"
#include "TsObject.h"
#include <cstdio>
#include <cstdlib>

extern "C" {

// All vm module methods call this stub which throws an error
void* ts_vm_not_supported(void* methodName) {
    TsString* name = (TsString*)methodName;
    const char* nameStr = name ? name->ToUtf8() : "unknown";

    fprintf(stderr, "Error: vm.%s() is not supported in AOT-compiled code.\n", nameStr);
    fprintf(stderr, "The vm module requires runtime code evaluation which is incompatible with ahead-of-time compilation.\n");
    exit(1);

    return nullptr;
}

} // extern "C"
