#include "IRGenerator.h"

namespace ts {

bool IRGenerator::tryGenerateBufferCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    if (prop->name == "alloc") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* size = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_buffer_alloc", ft);
        lastValue = createCall(ft, fn.getCallee(), { size });
        return true;
    } else if (prop->name == "from") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* data = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_buffer_from", ft);
        lastValue = createCall(ft, fn.getCallee(), { data });
        return true;
    } else if (prop->name == "toString") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction("ts_buffer_to_string", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj });
        return true;
    }
    
    return false;
}

bool IRGenerator::tryGenerateBufferPropertyAccess(ast::PropertyAccessExpression* node) {
    return false;
}

} // namespace ts
