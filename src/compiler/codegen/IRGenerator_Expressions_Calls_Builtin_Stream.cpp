#include "IRGenerator.h"

namespace ts {

bool IRGenerator::tryGenerateStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    if (prop->name == "write") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
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
        llvm::Value* obj = lastValue;
        
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
