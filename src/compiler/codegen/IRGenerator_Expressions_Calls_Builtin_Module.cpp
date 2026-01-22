#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace ts {
using namespace ast;

// Static helper to register module's runtime functions once
static bool moduleFunctionsRegistered = false;
static void ensureModuleFunctionsRegistered(BoxingPolicy& bp) {
    if (moduleFunctionsRegistered) return;
    moduleFunctionsRegistered = true;

    // module.builtinModules - returns boxed array
    bp.registerRuntimeApi("ts_module_get_builtin_modules", {}, true);

    // module.isBuiltin(name) - takes string, returns bool
    bp.registerRuntimeApi("ts_module_is_builtin", {false}, false);

    // module.createRequire(path) - takes string, returns boxed function
    bp.registerRuntimeApi("ts_module_create_require", {false}, true);

    // module.syncBuiltinESMExports() - no args, returns void
    bp.registerRuntimeApi("ts_module_sync_builtin_esm_exports", {}, false);

    // module.register(specifier, parentURL?, options?) - no-op stub
    bp.registerRuntimeApi("ts_module_register_loader", {false, false, false}, false);

    // module.registerHooks(hooks) - returns object with deregister
    bp.registerRuntimeApi("ts_module_register_hooks", {false}, true);

    // module.findPackageJSON(startPath?) - returns string or null
    bp.registerRuntimeApi("ts_module_find_package_json", {false}, true);

    // SourceMap constructor
    bp.registerRuntimeApi("ts_module_sourcemap_create", {false}, true);

    // SourceMap.findEntry(line, col)
    bp.registerRuntimeApi("ts_module_sourcemap_find_entry", {false, false, false}, true);
}

bool IRGenerator::tryGenerateModuleCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Register boxing info for module functions on first call
    ensureModuleFunctionsRegistered(boxingPolicy);

    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "module") return false;

    const std::string& methodName = prop->name;

    // ========================================================================
    // module.isBuiltin(moduleName: string): boolean
    // ========================================================================
    if (methodName == "isBuiltin") {
        if (node->arguments.empty()) return false;

        visit(node->arguments[0].get());
        llvm::Value* name = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getInt1Ty(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_is_builtin", ft);
        lastValue = createCall(ft, fn.getCallee(), { name });
        return true;
    }

    // ========================================================================
    // module.createRequire(filename: string): RequireFunction
    // ========================================================================
    if (methodName == "createRequire") {
        if (node->arguments.empty()) return false;

        visit(node->arguments[0].get());
        llvm::Value* path = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_create_require", ft);
        lastValue = createCall(ft, fn.getCallee(), { path });
        boxedValues.insert(lastValue);
        return true;
    }

    // ========================================================================
    // module.syncBuiltinESMExports(): void
    // ========================================================================
    if (methodName == "syncBuiltinESMExports") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_sync_builtin_esm_exports", ft);
        createCall(ft, fn.getCallee(), {});
        lastValue = nullptr;
        return true;
    }

    // ========================================================================
    // module.register(specifier: string, parentURL?: string, options?: object): void
    // Stub - loader hooks not applicable in AOT
    // ========================================================================
    if (methodName == "register") {
        // Accept 1-3 arguments, all optional after the first
        llvm::Value* specifier = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* parentURL = llvm::ConstantPointerNull::get(builder->getPtrTy());
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());

        if (node->arguments.size() >= 1) {
            visit(node->arguments[0].get());
            specifier = lastValue;
        }
        if (node->arguments.size() >= 2) {
            visit(node->arguments[1].get());
            parentURL = lastValue;
        }
        if (node->arguments.size() >= 3) {
            visit(node->arguments[2].get());
            options = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getVoidTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_register_loader", ft);
        createCall(ft, fn.getCallee(), { specifier, parentURL, options });
        lastValue = nullptr;
        return true;
    }

    // ========================================================================
    // module.registerHooks(hooks: object): { deregister: () => void }
    // Stub - loader hooks not applicable in AOT
    // ========================================================================
    if (methodName == "registerHooks") {
        llvm::Value* hooks = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            hooks = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_register_hooks", ft);
        lastValue = createCall(ft, fn.getCallee(), { hooks });
        boxedValues.insert(lastValue);
        return true;
    }

    // ========================================================================
    // module.findPackageJSON(startPath?: string): string | undefined
    // Stub - returns undefined in AOT context
    // ========================================================================
    if (methodName == "findPackageJSON") {
        llvm::Value* startPath = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            startPath = lastValue;
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder->getPtrTy(),
            { builder->getPtrTy() },
            false
        );
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_find_package_json", ft);
        lastValue = createCall(ft, fn.getCallee(), { startPath });
        boxedValues.insert(lastValue);
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateModulePropertyAccess(ast::PropertyAccessExpression* node) {
    auto id = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (!id || id->name != "module") return false;

    const std::string& propName = node->name;

    // ========================================================================
    // module.builtinModules: string[]
    // ========================================================================
    if (propName == "builtinModules") {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_get_builtin_modules", ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        boxedValues.insert(lastValue);
        return true;
    }

    // ========================================================================
    // module.SourceMap - returns the SourceMap class (stub)
    // Used as: new module.SourceMap(payload)
    // ========================================================================
    if (propName == "SourceMap") {
        // Return a sentinel value that will be handled in new expression
        // For now, return null - the actual construction is handled in tryGenerateModuleSourceMapNew
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateModuleSourceMapNew(ast::NewExpression* node) {
    // Check if this is `new module.SourceMap(payload)`
    auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get());
    if (!prop) return false;

    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "module" || prop->name != "SourceMap") return false;

    // Get the payload argument
    llvm::Value* payload = llvm::ConstantPointerNull::get(builder->getPtrTy());
    if (!node->arguments.empty()) {
        visit(node->arguments[0].get());
        payload = lastValue;
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(
        builder->getPtrTy(),
        { builder->getPtrTy() },
        false
    );
    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_module_sourcemap_create", ft);
    lastValue = createCall(ft, fn.getCallee(), { payload });
    boxedValues.insert(lastValue);
    return true;
}

} // namespace ts
