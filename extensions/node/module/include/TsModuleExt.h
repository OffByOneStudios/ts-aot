#ifndef TS_MODULE_EXT_H
#define TS_MODULE_EXT_H

#include <stdbool.h>

// Use void* instead of TsValue* for C compatibility - the actual implementation
// casts to TsValue* internally
#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Module Functions
// =============================================================================

// module.builtinModules - returns array of built-in module names
void* ts_module_get_builtin_modules();

// module.isBuiltin(name) - checks if module is built-in
bool ts_module_is_builtin(void* name);

// module.createRequire(path) - creates a require function bound to path
void* ts_module_create_require(void* path);

// module.syncBuiltinESMExports() - stub for ESM sync
void ts_module_sync_builtin_esm_exports();

// module.register(specifier, parentURL?, options?) - stub for loader hooks
void ts_module_register_loader(void* specifier, void* parentURL, void* options);

// module.registerHooks(hooks) - stub for loader hooks
void* ts_module_register_hooks(void* hooks);

// module.findPackageJSON(startPath?) - stub for package discovery
void* ts_module_find_package_json(void* startPath);

// =============================================================================
// SourceMap Functions
// =============================================================================

// SourceMap constructor
void* ts_module_sourcemap_create(void* payload);

// SourceMap.findEntry(line, col)
void* ts_module_sourcemap_find_entry(void* sourceMap, void* line, void* col);

// =============================================================================
// Dynamic Import
// =============================================================================

// Dynamic import() - returns Promise<module>
// For AOT compiler, this returns a rejected promise for non-builtin modules
void* ts_dynamic_import(void* moduleSpecifier);

#ifdef __cplusplus
}
#endif

#endif // TS_MODULE_EXT_H
