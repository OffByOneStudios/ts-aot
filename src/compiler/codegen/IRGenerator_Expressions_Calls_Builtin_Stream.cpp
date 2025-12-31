#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register Stream module's runtime functions once (6 functions)
static bool streamFunctionsRegistered = false;
static void ensureStreamFunctionsRegistered(BoxingPolicy& bp) {
    if (streamFunctionsRegistered) return;
    streamFunctionsRegistered = true;
    
    bp.registerRuntimeApi("ts_writable_write", {true, true}, true);  // stream, data -> promise
    bp.registerRuntimeApi("ts_writable_end", {true}, true);  // stream -> promise
    bp.registerRuntimeApi("ts_stream_pipe", {true, true}, true);  // src, dest -> dest
    bp.registerRuntimeApi("ts_stream_pause", {true}, false);  // stream
    bp.registerRuntimeApi("ts_stream_resume", {true}, false);  // stream
    bp.registerRuntimeApi("ts_value_get_object", {true}, true);  // boxed -> raw (helper)
}

bool IRGenerator::tryGenerateStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureStreamFunctionsRegistered(boxingPolicy);
    
    if (prop->name == "write") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        
        // Unbox if the expression is Any-typed
        llvm::Value* obj;
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_object", unboxFt);
            obj = createCall(unboxFt, unboxFn.getCallee(), { boxedObj });
        } else {
            obj = unboxValue(boxedObj, prop->expression->inferredType);
        }
        
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
        
        llvm::FunctionType* writeFt = llvm::FunctionType::get(builder->getInt1Ty(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_writable_write", writeFt);
        lastValue = createCall(writeFt, writeFn.getCallee(), { obj, data });
        return true;
    } else if (prop->name == "end") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        
        // Unbox if the expression is Any-typed
        llvm::Value* obj;
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_object", unboxFt);
            obj = createCall(unboxFt, unboxFn.getCallee(), { boxedObj });
        } else {
            obj = unboxValue(boxedObj, prop->expression->inferredType);
        }
        
        llvm::Value* data = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            data = boxValue(lastValue, node->arguments[0]->inferredType);
        }
        
        llvm::FunctionType* endFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee endFn = module->getOrInsertFunction("ts_writable_end", endFt);
        createCall(endFt, endFn.getCallee(), { obj, data });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "pipe") {
        visit(prop->expression.get());
        llvm::Value* src = lastValue;
        
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* dest = lastValue;
        
        llvm::FunctionType* pipeFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pipeFn = module->getOrInsertFunction("ts_stream_pipe", pipeFt);
        lastValue = createCall(pipeFt, pipeFn.getCallee(), { src, dest });
        return true;
    } else if (prop->name == "pause") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::FunctionType* pauseFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee pauseFn = module->getOrInsertFunction("ts_stream_pause", pauseFt);
        createCall(pauseFt, pauseFn.getCallee(), { obj });
        lastValue = obj;
        return true;
    } else if (prop->name == "resume") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::FunctionType* resumeFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee resumeFn = module->getOrInsertFunction("ts_stream_resume", resumeFt);
        createCall(resumeFt, resumeFn.getCallee(), { obj });
        lastValue = obj;
        return true;
    }
    
    return false;
}

} // namespace ts
