#include "IRGenerator.h"

namespace ts {

bool IRGenerator::tryGenerateStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    if (prop->name == "write") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* data = lastValue;
        
        if (node->arguments[0]->inferredType && node->arguments[0]->inferredType->kind == TypeKind::Any) {
            data = unboxValue(data, std::make_shared<Type>(TypeKind::Object));
        }

        llvm::FunctionType* writeFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee writeFn = module->getOrInsertFunction("ts_fs_write_stream_write", writeFt);
        createCall(writeFt, writeFn.getCallee(), { obj, data });
        lastValue = nullptr;
        return true;
    } else if (prop->name == "end") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::FunctionType* endFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee endFn = module->getOrInsertFunction("ts_fs_write_stream_end", endFt);
        createCall(endFt, endFn.getCallee(), { obj });
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
    }
    
    return false;
}

} // namespace ts
