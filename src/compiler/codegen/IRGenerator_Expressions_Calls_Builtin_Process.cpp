#include "IRGenerator.h"
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateProcessCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!obj || obj->name != "process") return false;

    if (prop->name == "exit") {
        llvm::Value* code = nullptr;
        if (node->arguments.empty()) {
            code = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        } else {
            visit(node->arguments[0].get());
            code = lastValue;
        }
        
        llvm::FunctionType* exitFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee exitFn = module->getOrInsertFunction("ts_process_exit", exitFt);
        
        createCall(exitFt, exitFn.getCallee(), { code });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "cwd") {
        llvm::FunctionType* cwdFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee cwdFn = module->getOrInsertFunction("ts_process_cwd", cwdFt);
        
        lastValue = unboxValue(createCall(cwdFt, cwdFn.getCallee(), {}), node->inferredType);
        return true;
    }

    if (prop->name == "chdir") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* dir = lastValue;

        llvm::FunctionType* chdirFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee chdirFn = module->getOrInsertFunction("ts_process_chdir", chdirFt);

        createCall(chdirFt, chdirFn.getCallee(), { dir });
        lastValue = nullptr;
        return true;
    }

    if (prop->name == "nextTick") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* callback = boxValue(lastValue, node->arguments[0]->inferredType);

        llvm::FunctionType* nextTickFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee nextTickFn = module->getOrInsertFunction("ts_process_next_tick", nextTickFt);

        createCall(nextTickFt, nextTickFn.getCallee(), { callback });
        lastValue = nullptr;
        return true;
    }

    return false;
}

} // namespace ts
