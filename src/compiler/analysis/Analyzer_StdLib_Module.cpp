#include "Analyzer.h"

namespace ts {

void Analyzer::registerModule() {
    auto moduleType = std::make_shared<ObjectType>();

    // ========================================================================
    // module.builtinModules: string[]
    // Array of all built-in module names
    // ========================================================================
    moduleType->fields["builtinModules"] = std::make_shared<ArrayType>(
        std::make_shared<Type>(TypeKind::String)
    );

    // ========================================================================
    // module.isBuiltin(moduleName: string): boolean
    // Returns true if the module is a Node.js built-in module
    // ========================================================================
    auto isBuiltinType = std::make_shared<FunctionType>();
    isBuiltinType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    isBuiltinType->returnType = std::make_shared<Type>(TypeKind::Boolean);
    moduleType->fields["isBuiltin"] = isBuiltinType;

    // ========================================================================
    // module.createRequire(filename: string): RequireFunction
    // Creates a require function bound to the specified path
    // ========================================================================
    auto createRequireType = std::make_shared<FunctionType>();
    createRequireType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    // Returns a function that takes a string and returns any
    auto requireFuncType = std::make_shared<FunctionType>();
    requireFuncType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));
    requireFuncType->returnType = std::make_shared<Type>(TypeKind::Any);
    createRequireType->returnType = requireFuncType;
    moduleType->fields["createRequire"] = createRequireType;

    // ========================================================================
    // module.syncBuiltinESMExports(): void
    // Syncs CommonJS exports to ESM live bindings for built-in modules
    // (Stub - not fully applicable in AOT context)
    // ========================================================================
    auto syncBuiltinESMExportsType = std::make_shared<FunctionType>();
    syncBuiltinESMExportsType->returnType = std::make_shared<Type>(TypeKind::Void);
    moduleType->fields["syncBuiltinESMExports"] = syncBuiltinESMExportsType;

    // ========================================================================
    // module.register(specifier: string, parentURL?: string, options?: object): void
    // Registers customization hooks for module loading
    // (Stub - loader hooks not applicable in AOT context)
    // ========================================================================
    auto registerType = std::make_shared<FunctionType>();
    registerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // specifier
    registerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // parentURL (optional)
    registerType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));     // options (optional)
    registerType->returnType = std::make_shared<Type>(TypeKind::Void);
    moduleType->fields["register"] = registerType;

    // ========================================================================
    // module.registerHooks(hooks: object): { deregister: () => void }
    // Registers module loading hooks
    // (Stub - loader hooks not applicable in AOT context)
    // ========================================================================
    auto registerHooksType = std::make_shared<FunctionType>();
    registerHooksType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));  // hooks object
    // Returns object with deregister function
    auto deregisterResultType = std::make_shared<ObjectType>();
    auto deregisterFnType = std::make_shared<FunctionType>();
    deregisterFnType->returnType = std::make_shared<Type>(TypeKind::Void);
    deregisterResultType->fields["deregister"] = deregisterFnType;
    registerHooksType->returnType = deregisterResultType;
    moduleType->fields["registerHooks"] = registerHooksType;

    // ========================================================================
    // module.findPackageJSON(startPath?: string): string | undefined
    // Finds the closest package.json from the given path
    // (Stub - returns undefined in AOT context)
    // ========================================================================
    auto findPackageJSONType = std::make_shared<FunctionType>();
    findPackageJSONType->paramTypes.push_back(std::make_shared<Type>(TypeKind::String));  // startPath (optional)
    findPackageJSONType->returnType = std::make_shared<Type>(TypeKind::String);  // or undefined
    moduleType->fields["findPackageJSON"] = findPackageJSONType;

    // ========================================================================
    // module.SourceMap class
    // Provides source map support
    // (Stub - source maps not applicable in AOT context)
    // ========================================================================
    auto sourceMapClass = std::make_shared<ClassType>("SourceMap");
    // Constructor: new SourceMap(payload: string | object)
    auto ctorType = std::make_shared<FunctionType>();
    ctorType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
    ctorType->returnType = sourceMapClass;
    sourceMapClass->constructorOverloads.push_back(ctorType);
    // findEntry(lineNumber: number, columnNumber: number): object | null
    auto findEntryType = std::make_shared<FunctionType>();
    findEntryType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    findEntryType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Int));
    findEntryType->returnType = std::make_shared<Type>(TypeKind::Any);
    sourceMapClass->methods["findEntry"] = findEntryType;
    // payload property
    sourceMapClass->fields["payload"] = std::make_shared<Type>(TypeKind::Any);
    moduleType->fields["SourceMap"] = sourceMapClass;

    symbols.define("module", moduleType);
}

} // namespace ts
