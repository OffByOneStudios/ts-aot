#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace ts {
using namespace ast;

// Static helper to register path module's runtime functions once
static bool pathFunctionsRegistered = false;
static void ensurePathFunctionsRegistered(BoxingPolicy& bp) {
    if (pathFunctionsRegistered) return;
    pathFunctionsRegistered = true;
    
    // Path property getters (no args, return string)
    bp.registerRuntimeApi("ts_path_get_sep", {}, false);
    bp.registerRuntimeApi("ts_path_get_sep_win32", {}, false);
    bp.registerRuntimeApi("ts_path_get_sep_posix", {}, false);
    bp.registerRuntimeApi("ts_path_get_delimiter", {}, false);
    bp.registerRuntimeApi("ts_path_get_delimiter_win32", {}, false);
    bp.registerRuntimeApi("ts_path_get_delimiter_posix", {}, false);
    
    // Path functions - single string arg, return string (not boxed)
    bp.registerRuntimeApi("ts_path_basename", {false}, false);
    bp.registerRuntimeApi("ts_path_basename_ex", {false, false}, false);
    bp.registerRuntimeApi("ts_path_dirname", {false}, false);
    bp.registerRuntimeApi("ts_path_dirname_ex", {false, false}, false);
    bp.registerRuntimeApi("ts_path_extname", {false}, false);
    bp.registerRuntimeApi("ts_path_extname_ex", {false, false}, false);
    bp.registerRuntimeApi("ts_path_normalize", {false}, false);
    bp.registerRuntimeApi("ts_path_normalize_ex", {false, false}, false);
    bp.registerRuntimeApi("ts_path_is_absolute", {false}, false);
    bp.registerRuntimeApi("ts_path_is_absolute_ex", {false, false}, false);
    bp.registerRuntimeApi("ts_path_to_namespaced_path", {false}, false);
    bp.registerRuntimeApi("ts_path_to_namespaced_path_ex", {false, false}, false);
    
    // Path functions - two string args
    bp.registerRuntimeApi("ts_path_relative", {false, false}, false);
    bp.registerRuntimeApi("ts_path_relative_ex", {false, false, false}, false);
    
    // Variadic path functions (take boxed array)
    bp.registerRuntimeApi("ts_path_join_variadic", {true}, false);
    bp.registerRuntimeApi("ts_path_join_variadic_ex", {true, false}, false);
    bp.registerRuntimeApi("ts_path_resolve", {true}, false);
    bp.registerRuntimeApi("ts_path_resolve_ex", {true, false}, false);
    
    // Parse/format (work with objects)
    bp.registerRuntimeApi("ts_path_parse", {false}, true);   // returns boxed object
    bp.registerRuntimeApi("ts_path_parse_ex", {false, false}, true);
    bp.registerRuntimeApi("ts_path_format", {true}, false);  // takes boxed object
    bp.registerRuntimeApi("ts_path_format_ex", {true, false}, false);
    
    // Array helpers used by path.join
    bp.registerRuntimeApi("ts_array_create_sized", {false}, true);  // size arg (raw int), returns boxed
    bp.registerRuntimeApi("ts_array_set", {true, false, true}, false);  // array (boxed), index (raw), value (boxed)
}

bool IRGenerator::tryGeneratePathCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Register boxing info for path functions on first call
    ensurePathFunctionsRegistered(boxingPolicy);
    
    bool isPath = false;
    int platform = 0; // 0: default, 1: win32, 2: posix
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "path") isPath = true;
    } else if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
            if (id->name == "path") {
                if (innerProp->name == "win32") {
                    isPath = true;
                    platform = 1;
                } else if (innerProp->name == "posix") {
                    isPath = true;
                    platform = 2;
                }
            }
        }
    }

    if (!isPath) return false;

    if (!node) {
        // Property access
        if (prop->name == "sep") {
            const char* fnName = "ts_path_get_sep";
            if (platform == 1) fnName = "ts_path_get_sep_win32";
            else if (platform == 2) fnName = "ts_path_get_sep_posix";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        } else if (prop->name == "delimiter") {
            const char* fnName = "ts_path_get_delimiter";
            if (platform == 1) fnName = "ts_path_get_delimiter_win32";
            else if (platform == 2) fnName = "ts_path_get_delimiter_posix";
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return true;
        } else if (prop->name == "win32" || prop->name == "posix") {
            // Return a dummy object or just undefined for now if not called
            lastValue = getUndefinedValue();
            return true;
        }
        return false;
    }

    if (prop->name == "join") {
        // path.join(...paths)
        std::vector<llvm::Value*> args;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(boxValue(lastValue, arg->inferredType));
        }

        llvm::FunctionType* arrayCreateFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee arrayCreateFn = getRuntimeFunction("ts_array_create_sized", arrayCreateFt);
        llvm::Value* array = createCall(arrayCreateFt, arrayCreateFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), args.size()) });

        for (size_t i = 0; i < args.size(); ++i) {
            // Use inline array set operation
            emitInlineArraySet(array, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i), args[i]);
        }

        if (platform != 0) {
            llvm::FunctionType* joinFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee joinFn = getRuntimeFunction("ts_path_join_variadic_ex", joinFt);
            lastValue = createCall(joinFt, joinFn.getCallee(), { array, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* joinFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee joinFn = getRuntimeFunction("ts_path_join_variadic", joinFt);
            lastValue = createCall(joinFt, joinFn.getCallee(), { array });
        }
        return true;
    } else if (prop->name == "resolve") {
        std::vector<llvm::Value*> args;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(boxValue(lastValue, arg->inferredType));
        }

        llvm::FunctionType* arrayCreateFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee arrayCreateFn = getRuntimeFunction("ts_array_create_sized", arrayCreateFt);
        llvm::Value* array = createCall(arrayCreateFt, arrayCreateFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), args.size()) });

        for (size_t i = 0; i < args.size(); ++i) {
            // Use inline array set operation
            emitInlineArraySet(array, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i), args[i]);
        }

        if (platform != 0) {
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee resolveFn = getRuntimeFunction("ts_path_resolve_ex", resolveFt);
            lastValue = createCall(resolveFt, resolveFn.getCallee(), { array, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee resolveFn = getRuntimeFunction("ts_path_resolve", resolveFt);
            lastValue = createCall(resolveFt, resolveFn.getCallee(), { array });
        }
        return true;
    } else if (prop->name == "normalize") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_normalize_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_normalize", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        return true;
    } else if (prop->name == "isAbsolute") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_is_absolute_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_is_absolute", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        lastValue = builder->CreateICmpNE(lastValue, builder->getInt32(0));
        return true;
    } else if (prop->name == "basename") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        llvm::Value* ext = nullptr;
        if (node->arguments.size() >= 2) {
            visit(node->arguments[1].get());
            ext = boxValue(lastValue, node->arguments[1]->inferredType);
        } else {
            ext = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_basename_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, ext, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_basename", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, ext });
        }
        return true;
    } else if (prop->name == "dirname") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_dirname_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_dirname", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        return true;
    } else if (prop->name == "extname") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_extname_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_extname", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        return true;
    } else if (prop->name == "relative") {
        if (node->arguments.size() < 2) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* from = boxValue(lastValue, node->arguments[0]->inferredType);
        visit(node->arguments[1].get());
        llvm::Value* to = boxValue(lastValue, node->arguments[1]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_relative_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { from, to, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_relative", ft);
            lastValue = createCall(ft, fn.getCallee(), { from, to });
        }
        return true;
    } else if (prop->name == "parse") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_parse_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_parse", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        return true;
    } else if (prop->name == "format") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_format_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_format", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        return true;
    } else if (prop->name == "toNamespacedPath") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        if (platform != 0) {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt32Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_to_namespaced_path_ex", ft);
            lastValue = createCall(ft, fn.getCallee(), { p, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform) });
        } else {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_path_to_namespaced_path", ft);
            lastValue = createCall(ft, fn.getCallee(), { p });
        }
        return true;
    }

    return false;
}

bool IRGenerator::tryGeneratePathPropertyAccess(ast::PropertyAccessExpression* node) {
    return tryGeneratePathCall(nullptr, node);
}

} // namespace ts
