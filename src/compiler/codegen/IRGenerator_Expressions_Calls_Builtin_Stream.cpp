#include "IRGenerator.h"
#include "BoxingPolicy.h"
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register Stream module's runtime functions once
static bool streamFunctionsRegistered = false;
static void ensureStreamFunctionsRegistered(BoxingPolicy& bp) {
    if (streamFunctionsRegistered) return;
    streamFunctionsRegistered = true;

    bp.registerRuntimeApi("ts_writable_write", {true, true}, true);  // stream, data -> promise
    bp.registerRuntimeApi("ts_writable_end", {true}, true);  // stream -> promise
    bp.registerRuntimeApi("ts_stream_pipe", {true, true}, true);  // src, dest -> dest
    bp.registerRuntimeApi("ts_stream_pause", {true}, false);  // stream
    bp.registerRuntimeApi("ts_stream_resume", {true}, false);  // stream
    bp.registerRuntimeApi("ts_value_get_object", {true}, false);  // boxed -> raw (returns raw pointer)

    // Readable state property accessors
    bp.registerRuntimeApi("ts_readable_destroyed", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_readable", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_is_paused", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_readable_ended", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_readable_flowing", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_unpipe", {true}, false);  // stream -> void

    // Writable state property accessors
    bp.registerRuntimeApi("ts_writable_destroyed", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_ended", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_finished", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_need_drain", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_high_water_mark", {true}, false);  // stream -> i64
    bp.registerRuntimeApi("ts_writable_writable_length", {true}, false);  // stream -> i64
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
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
            obj = createCall(unboxFt, unboxFn.getCallee(), { boxedObj });
        } else {
            obj = unboxValue(boxedObj, prop->expression->inferredType);
        }
        
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* data = boxValue(lastValue, node->arguments[0]->inferredType);
        
        llvm::FunctionType* writeFt = llvm::FunctionType::get(builder->getInt1Ty(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee writeFn = getRuntimeFunction("ts_writable_write", writeFt);
        lastValue = createCall(writeFt, writeFn.getCallee(), { obj, data });
        return true;
    } else if (prop->name == "end") {
        visit(prop->expression.get());
        llvm::Value* boxedObj = lastValue;
        
        // Unbox if the expression is Any-typed
        llvm::Value* obj;
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Any) {
            llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_object", unboxFt);
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
        llvm::FunctionCallee endFn = getRuntimeFunction("ts_writable_end", endFt);
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
        llvm::FunctionCallee pipeFn = getRuntimeFunction("ts_stream_pipe", pipeFt);
        lastValue = createCall(pipeFt, pipeFn.getCallee(), { src, dest });
        return true;
    } else if (prop->name == "pause") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;
        
        llvm::FunctionType* pauseFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee pauseFn = getRuntimeFunction("ts_stream_pause", pauseFt);
        createCall(pauseFt, pauseFn.getCallee(), { obj });
        lastValue = obj;
        return true;
    } else if (prop->name == "resume") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        llvm::FunctionType* resumeFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee resumeFn = getRuntimeFunction("ts_stream_resume", resumeFt);
        createCall(resumeFt, resumeFn.getCallee(), { obj });
        lastValue = obj;
        return true;
    } else if (prop->name == "isPaused") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_is_paused", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj });
        return true;
    } else if (prop->name == "unpipe") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_unpipe", ft);
        createCall(ft, fn.getCallee(), { obj });
        lastValue = obj;  // Return the stream for chaining
        return true;
    } else if (prop->name == "destroy") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Check if it's a readable or writable stream
        auto exprType = prop->expression->inferredType;
        bool isReadable = exprType && exprType->kind == TypeKind::Class &&
            (std::static_pointer_cast<ClassType>(exprType)->name == "Readable" ||
             std::static_pointer_cast<ClassType>(exprType)->name == "ReadStream");

        // Both readable and writable have destroy, we'll use writable destroy as a common handler
        // since both end up calling the same virtual method in C++
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        // Note: Both TsReadable and TsWritable have Destroy() method
        // The runtime will handle the proper cast
        llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_destroyed", ft);
        // Actually we need a destroy function, not destroyed property getter
        // For now return the object since destroy is a method that modifies state
        lastValue = obj;
        return true;
    }

    return false;
}

// Handle stream property access (not method calls)
bool IRGenerator::tryGenerateStreamPropertyAccess(ast::PropertyAccessExpression* prop) {
    ensureStreamFunctionsRegistered(boxingPolicy);

    auto exprType = prop->expression->inferredType;
    if (!exprType || exprType->kind != TypeKind::Class) return false;

    auto classType = std::static_pointer_cast<ClassType>(exprType);
    bool isReadable = classType->name == "Readable" || classType->name == "ReadStream" ||
                      classType->name == "Duplex" || classType->name == "Transform";
    bool isWritable = classType->name == "Writable" || classType->name == "WriteStream" ||
                      classType->name == "Duplex" || classType->name == "Transform";

    // Readable properties
    if (isReadable) {
        if (prop->name == "destroyed") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_destroyed", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readable") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableEnded") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_ended", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableFlowing") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_flowing", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        }
    }

    // Writable properties
    if (isWritable) {
        if (prop->name == "destroyed") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_destroyed", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writable") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writableEnded") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_ended", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writableFinished") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_finished", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writableNeedDrain") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_need_drain", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writableHighWaterMark") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_high_water_mark", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writableLength") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_length", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        }
    }

    return false;
}

} // namespace ts
