// Module extension for ts-aot
// Implementation of Node.js module module utilities.
// Built as a separate library (ts_module) linked when module module is imported.

#include "TsModuleExt.h"
#include "TsRuntime.h"
#include "TsString.h"
#include "TsArray.h"
#include "TsObject.h"
#include "TsMap.h"
#include "TsPromise.h"
#include "TsError.h"
#include "GC.h"
#include <cstring>
#include <cstdio>

// List of built-in modules supported by ts-aot
// This should match the modules in ModuleResolver.cpp
static const char* BUILTIN_MODULES[] = {
    "assert",
    "async_hooks",
    "buffer",
    "child_process",
    "cluster",
    "console",
    "crypto",
    "dgram",
    "dns",
    "events",
    "fs",
    "http",
    "http2",
    "https",
    "inspector",
    "module",
    "net",
    "os",
    "path",
    "perf_hooks",
    "process",
    "querystring",
    "readline",
    "stream",
    "string_decoder",
    "timers",
    "timers/promises",
    "tls",
    "tty",
    "url",
    "util",
    "zlib"
};

static const size_t BUILTIN_MODULES_COUNT = sizeof(BUILTIN_MODULES) / sizeof(BUILTIN_MODULES[0]);

extern "C" {

// module.builtinModules - returns array of built-in module names
void* ts_module_get_builtin_modules() {
    TsArray* arr = TsArray::Create();

    for (size_t i = 0; i < BUILTIN_MODULES_COUNT; i++) {
        TsString* name = TsString::Create(BUILTIN_MODULES[i]);
        arr->Push((int64_t)ts_value_make_string(name));
    }

    return ts_value_make_object(arr);
}

// module.isBuiltin(name) - checks if module is built-in
bool ts_module_is_builtin(void* nameVal) {
    // The argument may be a raw TsString* or a boxed TsValue*
    // First, try to use it as a raw TsString*
    TsString* name = (TsString*)nameVal;

    // Check if it might be a boxed value by looking at the value
    // If the first few bytes look like a TsValue type enum (0-10), try to unbox
    TsValue* maybeVal = (TsValue*)nameVal;
    if ((uint8_t)maybeVal->type <= 10) {
        void* raw = ts_value_get_object(maybeVal);
        if (raw && raw != nameVal) {
            name = (TsString*)raw;
        }
    }

    if (!name) return false;

    const char* cstr = name->ToUtf8();
    if (!cstr) return false;

    // Strip "node:" prefix if present (e.g., "node:fs" -> "fs")
    if (strncmp(cstr, "node:", 5) == 0) {
        cstr += 5;
    }

    // Check against builtin list
    for (size_t i = 0; i < BUILTIN_MODULES_COUNT; i++) {
        if (strcmp(cstr, BUILTIN_MODULES[i]) == 0) {
            return true;
        }
    }

    return false;
}

// module.createRequire(path) - creates a require function bound to path
// Note: In ts-aot, this is a stub that returns a placeholder.
// The actual require mechanism is handled at compile time.
void* ts_module_create_require(void* path) {
    // In a full implementation, this would create a require function
    // bound to the specified path. Since ts-aot handles requires at
    // compile time, we return a stub function.
    //
    // For now, return null to indicate this is not fully implemented
    // at runtime level (the compile-time require handling is what matters).
    (void)path;
    return ts_value_make_null();
}

// module.syncBuiltinESMExports() - stub for ESM sync
// This is a no-op in ts-aot since we don't have separate ESM/CJS at runtime
void ts_module_sync_builtin_esm_exports() {
    // No-op stub
}

// module.register(specifier, parentURL?, options?) - stub for loader hooks
// Loader hooks are not applicable in AOT context
void ts_module_register_loader(void* specifier, void* parentURL, void* options) {
    // No-op stub - loader hooks not applicable in AOT context
    (void)specifier;
    (void)parentURL;
    (void)options;
}

// module.registerHooks(hooks) - stub returning object with deregister function
// Loader hooks are not applicable in AOT context
void* ts_module_register_hooks(void* hooks) {
    (void)hooks;

    // Return object with a deregister function that does nothing
    TsMap* result = TsMap::Create();
    // Note: In a full implementation, we would add a 'deregister' function property
    // For now, return an empty object - callers can call .deregister() which will be undefined
    return ts_value_make_object(result);
}

// module.findPackageJSON(startPath?) - stub for package discovery
// Returns undefined in AOT context (package.json not applicable at runtime)
void* ts_module_find_package_json(void* startPath) {
    (void)startPath;
    // Return undefined - package discovery not applicable in AOT
    return ts_value_make_undefined();
}

// SourceMap constructor - stub for source map support
// Source maps are not applicable in AOT context
void* ts_module_sourcemap_create(void* payload) {
    // Create a stub SourceMap object with the payload property
    TsMap* sourceMap = TsMap::Create();

    // Store the payload (even though we don't use it)
    if (payload) {
        TsString* payloadKey = TsString::Create("payload");
        sourceMap->Set(payloadKey, (TsValue*)payload);
    }

    return ts_value_make_object(sourceMap);
}

// SourceMap.findEntry(line, col) - stub returning null
// Source maps are not applicable in AOT context
void* ts_module_sourcemap_find_entry(void* sourceMap, void* line, void* col) {
    (void)sourceMap;
    (void)line;
    (void)col;
    // Return null - source map lookup not applicable in AOT
    return ts_value_make_null();
}

// Dynamic import() - returns Promise<module>
// For string-literal specifiers resolved at compile time, the Monomorphizer rewrites
// import('literal') to ts_dynamic_import('absolutePath'). The module init has already
// run by the time this is called, so we check the module cache first.
// For unresolved specifiers, returns a rejected promise.
void* ts_dynamic_import(void* moduleSpecifier) {
    using namespace ts;

    // Get the module name from the specifier
    TsString* name = nullptr;
    if (moduleSpecifier) {
        TsValue* maybeVal = (TsValue*)moduleSpecifier;
        if ((uint8_t)maybeVal->type <= 10) {
            void* raw = ts_value_get_object(maybeVal);
            if (raw) {
                name = (TsString*)raw;
            } else {
                void* strRaw = ts_value_get_string(maybeVal);
                if (strRaw) {
                    name = (TsString*)strRaw;
                }
            }
        } else {
            name = (TsString*)moduleSpecifier;
        }
    }

    const char* moduleName = name ? name->ToUtf8() : "unknown";

    // Try the module cache first — the module may have been compiled statically
    if (name) {
        TsValue* boxedPath = ts_value_make_string(name);
        TsValue* exports = ts_module_get_cached(boxedPath);
        if (exports && !ts_value_is_undefined(exports)) {
            // Module found in cache — resolve the promise with its exports
            TsPromise* promise = ts_promise_create();
            ts_promise_resolve_internal(promise, exports);
            return ts_value_make_object(promise);
        }
    }

    // Module not in cache — return a rejected promise
    TsPromise* promise = ts_promise_create();

    char errorMsg[256];
    snprintf(errorMsg, sizeof(errorMsg),
        "Dynamic import() is not supported at runtime in AOT compilation. "
        "Module '%s' must be imported statically at compile time.",
        moduleName);

    TsString* errorStr = TsString::Create(errorMsg);
    void* error = ts_error_create(errorStr);

    ts_promise_reject_internal(promise, (TsValue*)error);

    return ts_value_make_object(promise);
}

} // extern "C"
