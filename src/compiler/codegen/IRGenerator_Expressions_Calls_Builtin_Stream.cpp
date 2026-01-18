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
    bp.registerRuntimeApi("ts_readable_readable_high_water_mark", {true}, false);  // stream -> i64
    bp.registerRuntimeApi("ts_readable_readable_length", {true}, false);  // stream -> i64
    bp.registerRuntimeApi("ts_readable_readable_object_mode", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_readable_aborted", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_readable_did_read", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_readable_set_encoding", {true, true}, true);  // stream, encoding -> stream
    bp.registerRuntimeApi("ts_readable_readable_encoding", {true}, true);  // stream -> string|null
    bp.registerRuntimeApi("ts_readable_unshift", {true, true}, false);  // stream, chunk -> void
    bp.registerRuntimeApi("ts_readable_read", {true, false}, true);  // stream, size -> any (data or null)

    // Stream module functions
    bp.registerRuntimeApi("ts_stream_pipeline", {true, true}, true);  // streams array, callback -> last stream
    bp.registerRuntimeApi("ts_stream_finished", {true, true, true}, true);  // stream, options/callback, callback -> cleanup fn

    // Writable state property accessors
    bp.registerRuntimeApi("ts_writable_destroyed", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_ended", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_finished", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_need_drain", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_writable_high_water_mark", {true}, false);  // stream -> i64
    bp.registerRuntimeApi("ts_writable_writable_length", {true}, false);  // stream -> i64
    bp.registerRuntimeApi("ts_writable_writable_object_mode", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_cork", {true}, false);  // stream -> void
    bp.registerRuntimeApi("ts_writable_uncork", {true}, false);  // stream -> void
    bp.registerRuntimeApi("ts_writable_writable_aborted", {true}, false);  // stream -> bool
    bp.registerRuntimeApi("ts_writable_set_default_encoding", {true, true}, true);  // stream, encoding -> stream

    // Transform stream functions
    bp.registerRuntimeApi("ts_stream_transform_create", {true}, true);  // options -> transform
    bp.registerRuntimeApi("ts_stream_transform_push", {true, true}, false);  // transform, data -> void
    bp.registerRuntimeApi("ts_stream_transform_push_null", {true}, false);  // transform -> void
    bp.registerRuntimeApi("ts_stream_transform_set_transform", {true, true}, false);  // transform, callback -> void
    bp.registerRuntimeApi("ts_stream_transform_set_flush", {true, true}, false);  // transform, callback -> void

    // Readable.from() static method
    bp.registerRuntimeApi("ts_readable_from", {true}, true);  // iterable -> readable

    // readable.wrap() method
    bp.registerRuntimeApi("ts_readable_wrap", {true, true}, true);  // readable, oldStream -> readable
}

bool IRGenerator::tryGenerateStreamModuleCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureStreamFunctionsRegistered(boxingPolicy);

    // Check for stream.Readable.from() - nested property access
    if (auto nestedProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        auto moduleId = dynamic_cast<ast::Identifier*>(nestedProp->expression.get());
        if (moduleId && moduleId->name == "stream" && nestedProp->name == "Readable" && prop->name == "from") {
            // stream.Readable.from(iterable)
            if (node->arguments.empty()) {
                lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
                return true;
            }

            visit(node->arguments[0].get());
            llvm::Value* iterable = boxValue(lastValue, node->arguments[0]->inferredType);

            llvm::FunctionType* fromFt = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fromFn = getRuntimeFunction("ts_readable_from", fromFt);
            lastValue = createCall(fromFt, fromFn.getCallee(), { iterable });
            return true;
        }
    }

    // Check if this is stream.pipeline() or stream.finished()
    auto id = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (!id || id->name != "stream") return false;

    if (prop->name == "pipeline") {
        // stream.pipeline(...streams, callback)
        // We need to collect all arguments into an array and extract the callback
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Create an array to hold the streams (all but the last argument if it's a function)
        llvm::FunctionType* createArrayFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createArrayFn = getRuntimeFunction("ts_array_create", createArrayFt);
        llvm::Value* streamsArray = createCall(createArrayFt, createArrayFn.getCallee(), {});

        // Check if the last argument is a callback function
        int lastIdx = (int)node->arguments.size() - 1;
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        int numStreams = (int)node->arguments.size();

        if (lastIdx >= 0) {
            auto lastArgType = node->arguments[lastIdx]->inferredType;
            if (lastArgType && lastArgType->kind == TypeKind::Function) {
                // Last argument is a callback
                visit(node->arguments[lastIdx].get());
                callback = boxValue(lastValue, lastArgType);
                numStreams = lastIdx;  // Don't include callback in stream array
            }
        }

        // Push all stream arguments into the array
        llvm::FunctionType* pushFt = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

        for (int i = 0; i < numStreams; i++) {
            visit(node->arguments[i].get());
            llvm::Value* stream = boxValue(lastValue, node->arguments[i]->inferredType);
            createCall(pushFt, pushFn.getCallee(), { streamsArray, stream });
        }

        // Call ts_stream_pipeline(streams, callback)
        llvm::FunctionType* pipelineFt = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pipelineFn = getRuntimeFunction("ts_stream_pipeline", pipelineFt);
        lastValue = createCall(pipelineFt, pipelineFn.getCallee(), { streamsArray, callback });
        return true;
    }

    if (prop->name == "finished") {
        // stream.finished(stream, options?, callback)
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        // Get stream argument
        visit(node->arguments[0].get());
        llvm::Value* stream = boxValue(lastValue, node->arguments[0]->inferredType);

        // Get optional second argument (options or callback)
        llvm::Value* optionsOrCallback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            optionsOrCallback = boxValue(lastValue, node->arguments[1]->inferredType);
        }

        // Get optional third argument (callback if second was options)
        llvm::Value* callback = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 2) {
            visit(node->arguments[2].get());
            callback = boxValue(lastValue, node->arguments[2]->inferredType);
        }

        // Call ts_stream_finished(stream, optionsOrCallback, callback)
        llvm::FunctionType* finishedFt = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee finishedFn = getRuntimeFunction("ts_stream_finished", finishedFt);
        lastValue = createCall(finishedFt, finishedFn.getCallee(), { stream, optionsOrCallback, callback });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateStreamCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureStreamFunctionsRegistered(boxingPolicy);

    // First check for module-level stream functions
    if (tryGenerateStreamModuleCall(node, prop)) {
        return true;
    }

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
    } else if (prop->name == "cork") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_cork", ft);
        createCall(ft, fn.getCallee(), { obj });
        lastValue = obj;  // Return the stream for chaining
        return true;
    } else if (prop->name == "uncork") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_uncork", ft);
        createCall(ft, fn.getCallee(), { obj });
        lastValue = obj;  // Return the stream for chaining
        return true;
    } else if (prop->name == "setEncoding") {
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Get the encoding argument
        llvm::Value* encoding = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            encoding = lastValue;
        } else {
            encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_set_encoding", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, encoding });
        return true;
    } else if (prop->name == "unshift") {
        // readable.unshift(chunk) - push data back to the front of the internal buffer
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Get the chunk argument
        llvm::Value* chunk = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            chunk = boxValue(lastValue, node->arguments[0]->inferredType);
        } else {
            chunk = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_unshift", ft);
        createCall(ft, fn.getCallee(), { obj, chunk });
        lastValue = nullptr;  // void return
        return true;
    } else if (prop->name == "read") {
        // readable.read([size]) - pull data from the internal buffer
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Get the optional size argument (defaults to 0 which means no size limit)
        llvm::Value* size = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            size = lastValue;
            // Ensure it's an i64
            if (size->getType() != builder->getInt64Ty()) {
                if (size->getType()->isDoubleTy()) {
                    size = builder->CreateFPToSI(size, builder->getInt64Ty());
                } else if (size->getType()->isIntegerTy()) {
                    size = builder->CreateSExtOrTrunc(size, builder->getInt64Ty());
                }
            }
        } else {
            size = llvm::ConstantInt::get(builder->getInt64Ty(), -1);  // -1 means read all available
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getInt64Ty() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_read", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, size });
        return true;
    } else if (prop->name == "setDefaultEncoding") {
        // writable.setDefaultEncoding(encoding) - set the default encoding for write()
        visit(prop->expression.get());
        llvm::Value* obj = lastValue;

        // Get the encoding argument
        llvm::Value* encoding = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            encoding = lastValue;
        } else {
            encoding = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_set_default_encoding", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, encoding });
        return true;
    } else if (prop->name == "wrap") {
        // readable.wrap(oldStream) - wrap an old-style stream in a new Readable
        visit(prop->expression.get());
        llvm::Value* readable = lastValue;

        // Get the old stream argument
        llvm::Value* oldStream = nullptr;
        if (!node->arguments.empty()) {
            visit(node->arguments[0].get());
            oldStream = boxValue(lastValue, node->arguments[0]->inferredType);
        } else {
            oldStream = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_wrap", ft);
        lastValue = createCall(ft, fn.getCallee(), { readable, oldStream });
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
        } else if (prop->name == "readableHighWaterMark") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_high_water_mark", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableLength") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_length", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableObjectMode") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_object_mode", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableAborted") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_aborted", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableDidRead") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_did_read", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "readableEncoding") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_readable_readable_encoding", ft);
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
        } else if (prop->name == "writableObjectMode") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_object_mode", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        } else if (prop->name == "writableAborted") {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
                    { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_writable_writable_aborted", ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return true;
        }
    }

    return false;
}

} // namespace ts
