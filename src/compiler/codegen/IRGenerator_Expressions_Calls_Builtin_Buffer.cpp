#include "IRGenerator.h"
#include "BoxingPolicy.h"

namespace ts {

// Static helper to register Buffer module's runtime functions once
static bool bufferFunctionsRegistered = false;
static void ensureBufferFunctionsRegistered(BoxingPolicy& bp) {
    if (bufferFunctionsRegistered) return;
    bufferFunctionsRegistered = true;

    bp.registerRuntimeApi("ts_buffer_alloc", {false}, true);  // size -> Buffer
    bp.registerRuntimeApi("ts_buffer_alloc_unsafe", {false}, true);
    bp.registerRuntimeApi("ts_buffer_from_string", {false, false}, true);  // string, encoding
    bp.registerRuntimeApi("ts_buffer_concat", {true, false}, true);  // array, totalLength
    bp.registerRuntimeApi("ts_buffer_is_buffer", {true}, false);  // obj -> bool
    bp.registerRuntimeApi("ts_buffer_is_encoding", {false}, false);  // encoding -> bool
    bp.registerRuntimeApi("ts_buffer_to_string", {true, false, false, false}, false);  // buf, encoding, start, end
    bp.registerRuntimeApi("ts_buffer_slice", {true, false, false}, true);  // buf, start, end
    bp.registerRuntimeApi("ts_buffer_subarray", {true, false, false}, true);
    bp.registerRuntimeApi("ts_buffer_copy", {true, true, false, false, false}, false);  // src, target, targetStart, srcStart, srcEnd
    bp.registerRuntimeApi("ts_buffer_fill", {true, true, false, false, false}, true);  // buf, value, offset, end, encoding

    // Read methods - all take buf and offset, return value
    bp.registerRuntimeApi("ts_buffer_read_int8", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_uint8", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_int16le", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_int16be", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_uint16le", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_uint16be", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_int32le", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_int32be", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_uint32le", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_uint32be", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_floatle", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_floatbe", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_doublele", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_doublebe", {true, false}, false);
    bp.registerRuntimeApi("ts_buffer_read_bigint64le", {true, false}, true);  // returns BigInt (ptr)
    bp.registerRuntimeApi("ts_buffer_read_bigint64be", {true, false}, true);
    bp.registerRuntimeApi("ts_buffer_read_biguint64le", {true, false}, true);
    bp.registerRuntimeApi("ts_buffer_read_biguint64be", {true, false}, true);

    // Write methods - all take buf, value, offset, return new offset
    bp.registerRuntimeApi("ts_buffer_write_int8", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_uint8", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_int16le", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_int16be", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_uint16le", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_uint16be", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_int32le", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_int32be", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_uint32le", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_uint32be", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_floatle", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_floatbe", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_doublele", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_doublebe", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_bigint64le", {true, false, false}, false);  // buf, bigint value, offset
    bp.registerRuntimeApi("ts_buffer_write_bigint64be", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_biguint64le", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_write_biguint64be", {true, false, false}, false);

    // Utility methods
    bp.registerRuntimeApi("ts_buffer_compare", {true, true}, false);  // buf1, buf2 -> int
    bp.registerRuntimeApi("ts_buffer_equals", {true, true}, false);   // buf, other -> bool
    bp.registerRuntimeApi("ts_buffer_indexof", {true, false, false}, false);  // buf, value, byteOffset -> int
    bp.registerRuntimeApi("ts_buffer_lastindexof", {true, false, false}, false);
    bp.registerRuntimeApi("ts_buffer_includes", {true, false, false}, false);  // buf, value, byteOffset -> bool

    // Iterator methods
    bp.registerRuntimeApi("ts_buffer_entries", {true}, false);  // buf -> array of [index, byte] pairs
    bp.registerRuntimeApi("ts_buffer_keys", {true}, false);     // buf -> array of indices
    bp.registerRuntimeApi("ts_buffer_values", {true}, false);   // buf -> array of byte values
    bp.registerRuntimeApi("ts_buffer_to_json", {true}, false);  // buf -> object { type, data }
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
    // Must check it's actually Buffer.from, not Array.from or other X.from
    auto* bufferId = dynamic_cast<ast::Identifier*>(prop->expression.get());
    if (prop->name == "from" && bufferId && bufferId->name == "Buffer") {
        if (node->arguments.empty()) return true;

        // Check the argument type to determine which runtime function to call
        auto* arg0 = node->arguments[0].get();
        bool isBuffer = false;
        bool isArray = false;
        if (arg0->inferredType) {
            if (arg0->inferredType->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(arg0->inferredType);
                if (classType->name == "Buffer") isBuffer = true;
            } else if (arg0->inferredType->kind == TypeKind::Array) {
                isArray = true;
            }
        }

        visit(arg0);
        llvm::Value* data = lastValue;

        if (isBuffer) {
            // Buffer.from(buffer) - copy the buffer
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_from_buffer", ft);
            lastValue = createCall(ft, fn.getCallee(), { data });
            return true;
        }

        if (isArray) {
            // Buffer.from(array) - create buffer from array of numbers
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_from_array", ft);
            lastValue = createCall(ft, fn.getCallee(), { data });
            return true;
        }

        // Otherwise treat as string with optional encoding
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

    // Handle Buffer.isEncoding(encoding)
    if (prop->name == "isEncoding" && bufferId && bufferId->name == "Buffer") {
        if (node->arguments.empty()) return true;
        visit(node->arguments[0].get());
        llvm::Value* encoding = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_is_encoding", ft);
        lastValue = createCall(ft, fn.getCallee(), { encoding });
        return true;
    }

    // Handle Buffer.compare(buf1, buf2) - static method
    if (prop->name == "compare" && bufferId && bufferId->name == "Buffer") {
        if (node->arguments.size() < 2) return true;
        visit(node->arguments[0].get());
        llvm::Value* buf1 = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* buf2 = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_compare", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf1, buf2 });
        return true;
    }

    // Helper lambda for buffer read methods (offset -> value)
    auto genReadMethod = [&](const std::string& methodName, const std::string& rtName, bool returnsDouble) -> bool {
        if (prop->name == methodName && isBuffer) {
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::Type* returnType = returnsDouble ? builder->getDoubleTy() : llvm::Type::getInt64Ty(*context);
            llvm::FunctionType* ft = llvm::FunctionType::get(returnType,
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, offset });
            return true;
        }
        return false;
    };

    // Buffer read methods
    if (genReadMethod("readInt8", "ts_buffer_read_int8", false)) return true;
    if (genReadMethod("readUInt8", "ts_buffer_read_uint8", false)) return true;
    if (genReadMethod("readInt16LE", "ts_buffer_read_int16le", false)) return true;
    if (genReadMethod("readInt16BE", "ts_buffer_read_int16be", false)) return true;
    if (genReadMethod("readUInt16LE", "ts_buffer_read_uint16le", false)) return true;
    if (genReadMethod("readUInt16BE", "ts_buffer_read_uint16be", false)) return true;
    if (genReadMethod("readInt32LE", "ts_buffer_read_int32le", false)) return true;
    if (genReadMethod("readInt32BE", "ts_buffer_read_int32be", false)) return true;
    if (genReadMethod("readUInt32LE", "ts_buffer_read_uint32le", false)) return true;
    if (genReadMethod("readUInt32BE", "ts_buffer_read_uint32be", false)) return true;
    if (genReadMethod("readFloatLE", "ts_buffer_read_floatle", true)) return true;
    if (genReadMethod("readFloatBE", "ts_buffer_read_floatbe", true)) return true;
    if (genReadMethod("readDoubleLE", "ts_buffer_read_doublele", true)) return true;
    if (genReadMethod("readDoubleBE", "ts_buffer_read_doublebe", true)) return true;

    // Helper lambda for buffer BigInt read methods (offset -> BigInt ptr)
    auto genReadMethodBigInt = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, offset });
            return true;
        }
        return false;
    };

    // BigInt read methods
    if (genReadMethodBigInt("readBigInt64LE", "ts_buffer_read_bigint64le")) return true;
    if (genReadMethodBigInt("readBigInt64BE", "ts_buffer_read_bigint64be")) return true;
    if (genReadMethodBigInt("readBigUInt64LE", "ts_buffer_read_biguint64le")) return true;
    if (genReadMethodBigInt("readBigUInt64BE", "ts_buffer_read_biguint64be")) return true;

    // Helper lambda for buffer write methods (value, offset -> newOffset)
    auto genWriteMethodInt = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* value = castValue(lastValue, llvm::Type::getInt64Ty(*context));

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, value, offset });
            return true;
        }
        return false;
    };

    auto genWriteMethodDouble = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* value = castValue(lastValue, builder->getDoubleTy());

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                { builder->getPtrTy(), builder->getDoubleTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, value, offset });
            return true;
        }
        return false;
    };

    // Buffer write methods - integer types
    if (genWriteMethodInt("writeInt8", "ts_buffer_write_int8")) return true;
    if (genWriteMethodInt("writeUInt8", "ts_buffer_write_uint8")) return true;
    if (genWriteMethodInt("writeInt16LE", "ts_buffer_write_int16le")) return true;
    if (genWriteMethodInt("writeInt16BE", "ts_buffer_write_int16be")) return true;
    if (genWriteMethodInt("writeUInt16LE", "ts_buffer_write_uint16le")) return true;
    if (genWriteMethodInt("writeUInt16BE", "ts_buffer_write_uint16be")) return true;
    if (genWriteMethodInt("writeInt32LE", "ts_buffer_write_int32le")) return true;
    if (genWriteMethodInt("writeInt32BE", "ts_buffer_write_int32be")) return true;
    if (genWriteMethodInt("writeUInt32LE", "ts_buffer_write_uint32le")) return true;
    if (genWriteMethodInt("writeUInt32BE", "ts_buffer_write_uint32be")) return true;

    // Buffer write methods - float types
    if (genWriteMethodDouble("writeFloatLE", "ts_buffer_write_floatle")) return true;
    if (genWriteMethodDouble("writeFloatBE", "ts_buffer_write_floatbe")) return true;
    if (genWriteMethodDouble("writeDoubleLE", "ts_buffer_write_doublele")) return true;
    if (genWriteMethodDouble("writeDoubleBE", "ts_buffer_write_doublebe")) return true;

    // Helper lambda for buffer BigInt write methods (bigint value, offset -> newOffset)
    auto genWriteMethodBigInt = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* value = lastValue;  // BigInt pointer

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                { builder->getPtrTy(), builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, value, offset });
            return true;
        }
        return false;
    };

    // BigInt write methods
    if (genWriteMethodBigInt("writeBigInt64LE", "ts_buffer_write_bigint64le")) return true;
    if (genWriteMethodBigInt("writeBigInt64BE", "ts_buffer_write_bigint64be")) return true;
    if (genWriteMethodBigInt("writeBigUInt64LE", "ts_buffer_write_biguint64le")) return true;
    if (genWriteMethodBigInt("writeBigUInt64BE", "ts_buffer_write_biguint64be")) return true;

    // Helper lambda for variable-length read methods (offset, byteLength -> value)
    auto genVarReadMethod = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (!node->arguments.empty()) {
                visit(node->arguments[0].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::Value* byteLength = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                byteLength = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, offset, byteLength });
            return true;
        }
        return false;
    };

    // Variable-length read methods
    if (genVarReadMethod("readIntLE", "ts_buffer_read_intle")) return true;
    if (genVarReadMethod("readIntBE", "ts_buffer_read_intbe")) return true;
    if (genVarReadMethod("readUIntLE", "ts_buffer_read_uintle")) return true;
    if (genVarReadMethod("readUIntBE", "ts_buffer_read_uintbe")) return true;

    // Helper lambda for variable-length write methods (value, offset, byteLength -> newOffset)
    auto genVarWriteMethod = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            if (node->arguments.empty()) return true;
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            visit(node->arguments[0].get());
            llvm::Value* value = castValue(lastValue, llvm::Type::getInt64Ty(*context));

            llvm::Value* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            if (node->arguments.size() > 1) {
                visit(node->arguments[1].get());
                offset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::Value* byteLength = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 1);
            if (node->arguments.size() > 2) {
                visit(node->arguments[2].get());
                byteLength = castValue(lastValue, llvm::Type::getInt64Ty(*context));
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf, value, offset, byteLength });
            return true;
        }
        return false;
    };

    // Variable-length write methods
    if (genVarWriteMethod("writeIntLE", "ts_buffer_write_intle")) return true;
    if (genVarWriteMethod("writeIntBE", "ts_buffer_write_intbe")) return true;
    if (genVarWriteMethod("writeUIntLE", "ts_buffer_write_uintle")) return true;
    if (genVarWriteMethod("writeUIntBE", "ts_buffer_write_uintbe")) return true;

    // Buffer swap methods (returns this for chaining)
    auto genSwapMethod = [&](const std::string& methodName, const std::string& rtName) -> bool {
        if (prop->name == methodName && isBuffer) {
            visit(prop->expression.get());
            llvm::Value* buf = lastValue;

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
                { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction(rtName, ft);
            lastValue = createCall(ft, fn.getCallee(), { buf });
            return true;
        }
        return false;
    };

    if (genSwapMethod("swap16", "ts_buffer_swap16")) return true;
    if (genSwapMethod("swap32", "ts_buffer_swap32")) return true;
    if (genSwapMethod("swap64", "ts_buffer_swap64")) return true;

    // Handle buf.compare(other) - instance method
    if (prop->name == "compare" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;
        visit(node->arguments[0].get());
        llvm::Value* other = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_compare", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf, other });
        return true;
    }

    // Handle buf.equals(other)
    if (prop->name == "equals" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;
        visit(node->arguments[0].get());
        llvm::Value* other = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_equals", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf, other });
        return true;
    }

    // Handle buf.indexOf(value, byteOffset?)
    if (prop->name == "indexOf" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        visit(node->arguments[0].get());
        llvm::Value* value = castValue(lastValue, llvm::Type::getInt64Ty(*context));

        llvm::Value* byteOffset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            byteOffset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_indexof", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf, value, byteOffset });
        return true;
    }

    // Handle buf.lastIndexOf(value, byteOffset?)
    if (prop->name == "lastIndexOf" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        visit(node->arguments[0].get());
        llvm::Value* value = castValue(lastValue, llvm::Type::getInt64Ty(*context));

        // Default byteOffset is buffer.length (which we represent as -1 meaning "end")
        llvm::Value* byteOffset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), -1);
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            byteOffset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_lastindexof", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf, value, byteOffset });
        return true;
    }

    // Handle buf.includes(value, byteOffset?)
    if (prop->name == "includes" && isBuffer) {
        if (node->arguments.empty()) return true;
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        visit(node->arguments[0].get());
        llvm::Value* value = castValue(lastValue, llvm::Type::getInt64Ty(*context));

        llvm::Value* byteOffset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            byteOffset = castValue(lastValue, llvm::Type::getInt64Ty(*context));
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_includes", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf, value, byteOffset });
        return true;
    }

    // Handle buf.entries() - returns array of [index, byte] pairs
    if (prop->name == "entries" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_entries", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf });
        return true;
    }

    // Handle buf.keys() - returns array of indices
    if (prop->name == "keys" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_keys", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf });
        return true;
    }

    // Handle buf.values() - returns array of byte values
    if (prop->name == "values" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_values", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf });
        return true;
    }

    // Handle buf.toJSON() - returns { type: "Buffer", data: [...] }
    if (prop->name == "toJSON" && isBuffer) {
        visit(prop->expression.get());
        llvm::Value* buf = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_to_json", ft);
        lastValue = createCall(ft, fn.getCallee(), { buf });
        return true;
    }

    return false;
}

bool IRGenerator::tryGenerateBufferPropertyAccess(ast::PropertyAccessExpression* node) {
    return false;
}

} // namespace ts
