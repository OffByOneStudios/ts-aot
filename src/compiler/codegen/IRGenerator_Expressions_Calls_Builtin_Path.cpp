#include "IRGenerator.h"
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGeneratePathCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    bool isPath = false;
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "path") isPath = true;
    }

    if (!isPath) return false;

    if (!node) {
        // Property access
        if (prop->name == "sep") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_get_sep", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
            return true;
        } else if (prop->name == "delimiter") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_get_delimiter", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
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
        llvm::FunctionCallee arrayCreateFn = module->getOrInsertFunction("ts_array_create_sized", arrayCreateFt);
        llvm::Value* array = createCall(arrayCreateFt, arrayCreateFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), args.size()) });

        llvm::FunctionType* arraySetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee arraySetFn = module->getOrInsertFunction("ts_array_set", arraySetFt);

        for (size_t i = 0; i < args.size(); ++i) {
            createCall(arraySetFt, arraySetFn.getCallee(), { array, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i), args[i] });
        }

        llvm::FunctionType* joinFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee joinFn = module->getOrInsertFunction("ts_path_join_variadic", joinFt);
        lastValue = createCall(joinFt, joinFn.getCallee(), { array });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
        return true;
    } else if (prop->name == "resolve") {
        std::vector<llvm::Value*> args;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(boxValue(lastValue, arg->inferredType));
        }

        llvm::FunctionType* arrayCreateFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee arrayCreateFn = module->getOrInsertFunction("ts_array_create_sized", arrayCreateFt);
        llvm::Value* array = createCall(arrayCreateFt, arrayCreateFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), args.size()) });

        llvm::FunctionType* arraySetFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), builder->getPtrTy() }, false);
        llvm::FunctionCallee arraySetFn = module->getOrInsertFunction("ts_array_set", arraySetFt);

        for (size_t i = 0; i < args.size(); ++i) {
            createCall(arraySetFt, arraySetFn.getCallee(), { array, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i), args[i] });
        }

        llvm::FunctionType* resolveFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_path_resolve", resolveFt);
        lastValue = createCall(resolveFt, resolveFn.getCallee(), { array });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
        return true;
    } else if (prop->name == "normalize") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_normalize", ft);
        lastValue = createCall(ft, fn.getCallee(), { p });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
        return true;
    } else if (prop->name == "isAbsolute") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_is_absolute", ft);
        lastValue = createCall(ft, fn.getCallee(), { p });
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
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_basename", ft);
        lastValue = createCall(ft, fn.getCallee(), { p, ext });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
        return true;
    } else if (prop->name == "dirname") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_dirname", ft);
        lastValue = createCall(ft, fn.getCallee(), { p });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
        return true;
    } else if (prop->name == "extname") {
        if (node->arguments.size() < 1) {
            lastValue = getUndefinedValue();
            return true;
        }
        visit(node->arguments[0].get());
        llvm::Value* p = boxValue(lastValue, node->arguments[0]->inferredType);
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_extname", ft);
        lastValue = createCall(ft, fn.getCallee(), { p });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
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
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_path_relative", ft);
        lastValue = createCall(ft, fn.getCallee(), { from, to });
        lastValue = boxValue(lastValue, std::make_shared<Type>(TypeKind::String));
        return true;
    }

    return false;
}

bool IRGenerator::tryGeneratePathPropertyAccess(ast::PropertyAccessExpression* node) {
    return tryGeneratePathCall(nullptr, node);
}

} // namespace ts
