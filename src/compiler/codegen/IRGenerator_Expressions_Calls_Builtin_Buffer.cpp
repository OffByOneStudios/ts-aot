#include "IRGenerator.h"
#include "BoxingPolicy.h"

namespace ts {

// Static helper to register Buffer module's runtime functions once (10 functions)
static bool bufferFunctionsRegistered = false;
static void ensureBufferFunctionsRegistered(BoxingPolicy& bp) {
    if (bufferFunctionsRegistered) return;
    bufferFunctionsRegistered = true;
    
    bp.registerRuntimeApi("ts_buffer_alloc", {false}, true);  // size -> Buffer
    bp.registerRuntimeApi("ts_buffer_alloc_unsafe", {false}, true);
    bp.registerRuntimeApi("ts_buffer_from_string", {false, false}, true);  // string, encoding
    bp.registerRuntimeApi("ts_buffer_concat", {true, false}, true);  // array, totalLength
    bp.registerRuntimeApi("ts_buffer_is_buffer", {true}, false);  // obj -> bool
    bp.registerRuntimeApi("ts_buffer_to_string", {true, false, false, false}, false);  // buf, encoding, start, end
    bp.registerRuntimeApi("ts_buffer_slice", {true, false, false}, true);  // buf, start, end
    bp.registerRuntimeApi("ts_buffer_subarray", {true, false, false}, true);
    bp.registerRuntimeApi("ts_buffer_copy", {true, true, false, false, false}, false);  // src, target, targetStart, srcStart, srcEnd
    bp.registerRuntimeApi("ts_buffer_fill", {true, true, false, false, false}, true);  // buf, value, offset, end, encoding
}

bool IRGenerator::tryGenerateBufferCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureBufferFunctionsRegistered(boxingPolicy);
    
    // Handle Buffer.alloc(size)
    if (prop->name == "alloc") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* size = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_alloc", ft);
        lastValue = createCall(ft, fn.getCallee(), { size });
        return true;
    }
    
    // Handle Buffer.allocUnsafe(size)
    if (prop->name == "allocUnsafe") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* size = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_alloc_unsafe", ft);
        lastValue = createCall(ft, fn.getCallee(), { size });
        return true;
    }
    
    // Handle Buffer.from(data, encoding?)
    if (prop->name == "from") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* data = lastValue;
        
        llvm::Value* encoding;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            encoding = lastValue;
        } else {
            encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_from_string", ft);
        lastValue = createCall(ft, fn.getCallee(), { data, encoding });
        return true;
    }
    
    // Handle Buffer.concat(list, totalLength?) - static method on Buffer
    auto* bufferId = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (prop->name == "concat" && bufferId && bufferId->name == "Buffer") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* list = lastValue;
        
        llvm::Value* totalLength;
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            totalLength = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        } else {
            totalLength = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_concat", ft);
        lastValue = createCall(ft, fn.getCallee(), { list, totalLength });
        return true;
    }
    
    // Handle Buffer.isBuffer(obj)
    if (prop->name == "isBuffer") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* obj = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_is_buffer", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj });
        return true;
    }
    
    // Handle instance methods - need to check if expression is a buffer
    // Check if we're calling a method on a Buffer
    bool isBuffer = false;
    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        if (classType->name == "Buffer") isBuffer = true;
    }
    
    // Handle buf.toString(encoding?)
    if (prop->name == "toString" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::Value* encoding;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            encoding = lastValue;
        } else {
            encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_to_string", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, encoding });
        return true;
    }
    
    // Handle buf.slice(start, end)
    if (prop->name == "slice" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
        
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            start = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            end = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_slice", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, start, end });
        return true;
    }
    
    // Handle buf.subarray(start, end)
    if (prop->name == "subarray" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
        
        if (node->arguments.size() > 0) {
            visit(node->arguments[0].get());
            start = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            end = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_subarray", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, start, end });
        return true;
    }
    
    // Handle buf.fill(value, start?, end?)
    if (prop->name == "fill" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        visit(node->arguments[0].get());
        llvm::Value* value = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        
        llvm::Value* start = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* end = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
        
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            start = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            end = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_fill", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, value, start, end });
        return true;
    }
    
    // Handle buf.copy(target, targetStart?, sourceStart?, sourceEnd?)
    if (prop->name == "copy" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* source = lastValue;
        
        visit(node->arguments[0].get());
        llvm::Value* target = lastValue;
        
        llvm::Value* targetStart = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* sourceStart = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* sourceEnd = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
        
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            targetStart = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            sourceStart = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        if (node->arguments.size() > 3) {
            visit(node->arguments[3].get());
            sourceEnd = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), 
            { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), 
              llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_copy", ft);
        lastValue = createCall(ft, fn.getCallee(), { source, target, targetStart, sourceStart, sourceEnd });
        return true;
    }
    
    return false;
}

bool IRGenerator::tryGenerateBufferPropertyAccess(ast::PropertyAccessExpression* node) {
    return false;
}

} // namespace ts
