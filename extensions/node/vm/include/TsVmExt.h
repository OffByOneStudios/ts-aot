#ifndef TS_VM_EXT_H
#define TS_VM_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

// vm module stub functions
// The vm module is incompatible with AOT compilation as it requires runtime code evaluation.

// All vm module methods route through this stub which throws an error
void* ts_vm_not_supported(void* methodName);

// Script class method stubs for vtable generation
void* Script_runInContext(void* self, void* context, void* options);
void* Script_runInNewContext(void* self, void* context, void* options);
void* Script_runInThisContext(void* self, void* options);
void* Script_createCachedData(void* self);

#ifdef __cplusplus
}
#endif

#endif // TS_VM_EXT_H
