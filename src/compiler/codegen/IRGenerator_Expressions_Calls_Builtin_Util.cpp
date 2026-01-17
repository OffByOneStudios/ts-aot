#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include <spdlog/spdlog.h>

namespace ts {

// Static helper to register Util module's runtime functions once (22 functions)
static bool utilFunctionsRegistered = false;
static void ensureUtilFunctionsRegistered(BoxingPolicy& bp) {
    if (utilFunctionsRegistered) return;
    utilFunctionsRegistered = true;
    
    // Core util functions
    bp.registerRuntimeApi("ts_util_format", {true}, false);  // args array -> string
    bp.registerRuntimeApi("ts_util_inspect", {true, true}, false);  // obj, options -> string
    bp.registerRuntimeApi("ts_util_is_deep_strict_equal", {true, true}, false);  // a, b -> bool
    bp.registerRuntimeApi("ts_util_promisify", {true}, true);  // func -> promisified func
    bp.registerRuntimeApi("ts_util_callbackify", {true}, true);  // func -> callbackified func
    bp.registerRuntimeApi("ts_util_deprecate", {true, false, false}, true);  // func, msg, code
    bp.registerRuntimeApi("ts_util_inherits", {true, true}, false);  // ctor, superCtor
    bp.registerRuntimeApi("ts_util_strip_vt_control_characters", {true}, false);  // str -> str
    bp.registerRuntimeApi("ts_util_to_usv_string", {true}, false);  // str -> str
    
    // util.types.isXxx functions (all take boxed value, return bool)
    bp.registerRuntimeApi("ts_util_types_is_array_buffer", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_array_buffer_view", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_async_function", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_date", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_generator_function", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_generator_object", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_map", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_native_error", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_promise", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_reg_exp", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_set", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_typed_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_uint8_array", {true}, false);

    // Specific TypedArray type checks
    bp.registerRuntimeApi("ts_util_types_is_int8_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_int16_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_int32_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_uint8_clamped_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_uint16_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_uint32_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_float32_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_float64_array", {true}, false);
    bp.registerRuntimeApi("ts_util_types_is_data_view", {true}, false);

    // Array helpers used in util
    bp.registerRuntimeApi("ts_array_create_sized", {false}, true);
    bp.registerRuntimeApi("ts_array_push", {true, true}, false);
}

bool IRGenerator::tryGenerateUtilCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    ensureUtilFunctionsRegistered(boxingPolicy);
    
    // Check if this is a util.xxx call
    bool isUtil = false;
    bool isUtilTypes = false;
    
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        if (id->name == "util") {
            isUtil = true;
        }
    }
    
    // Check for util.types.xxx
    if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
            if (id->name == "util" && innerProp->name == "types") {
                isUtilTypes = true;
            }
        }
    }
    
    if (!isUtil && !isUtilTypes) return false;
    
    SPDLOG_DEBUG("tryGenerateUtilCall: {}.{}", isUtil ? "util" : "util.types", prop->name);
    
    // =========================================================================
    // util.format(format, ...args)
    // =========================================================================
    if (isUtil && prop->name == "format") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        
        // Get format string
        visit(node->arguments[0].get());
        llvm::Value* format = lastValue;
        
        // Create array of remaining args
        llvm::Value* argsArray;
        if (node->arguments.size() > 1) {
            // Create an EMPTY array, not a sized one (sized would pre-fill with nulls)
            llvm::FunctionType* createFT = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFT);
            argsArray = createCall(createFT, createFn.getCallee(), {});

            llvm::FunctionType* pushFT = llvm::FunctionType::get(builder->getVoidTy(),
                { builder->getPtrTy(), builder->getInt64Ty() }, false);
            llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFT);

            for (size_t i = 1; i < node->arguments.size(); i++) {
                visit(node->arguments[i].get());
                llvm::Value* arg = lastValue;
                // Box the value
                llvm::Value* boxed = boxValue(arg, node->arguments[i]->inferredType);
                llvm::Value* asInt = builder->CreatePtrToInt(boxed, builder->getInt64Ty());
                createCall(pushFT, pushFn.getCallee(), { argsArray, asInt });
            }
        } else {
            argsArray = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_util_format", ft);
        lastValue = createCall(ft, fn.getCallee(), { format, argsArray });
        return true;
    }
    
    // =========================================================================
    // util.inspect(obj, options?)
    // =========================================================================
    if (isUtil && prop->name == "inspect") {
        if (node->arguments.empty()) {
            lastValue = getUndefinedValue();
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* obj = lastValue;
        
        llvm::Value* options = llvm::ConstantPointerNull::get(builder->getPtrTy());
        if (node->arguments.size() > 1) {
            visit(node->arguments[1].get());
            options = lastValue;
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_util_inspect", ft);
        lastValue = createCall(ft, fn.getCallee(), { obj, options });
        return true;
    }
    
    // =========================================================================
    // util.promisify(fn)
    // =========================================================================
    if (isUtil && prop->name == "promisify") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* fn = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee promisify = getRuntimeFunction("ts_util_promisify", ft);
        lastValue = createCall(ft, promisify.getCallee(), { fn });
        return true;
    }
    
    // =========================================================================
    // util.inherits(constructor, superConstructor)
    // =========================================================================
    if (isUtil && prop->name == "inherits") {
        if (node->arguments.size() < 2) {
            lastValue = getUndefinedValue();
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* ctor = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* superCtor = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getVoidTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_util_inherits", ft);
        createCall(ft, fn.getCallee(), { ctor, superCtor });
        lastValue = getUndefinedValue();
        return true;
    }
    
    // =========================================================================
    // util.deprecate(fn, msg)
    // =========================================================================
    if (isUtil && prop->name == "deprecate") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* fn = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* msg = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee deprecate = getRuntimeFunction("ts_util_deprecate", ft);
        lastValue = createCall(ft, deprecate.getCallee(), { fn, msg });
        return true;
    }
    
    // =========================================================================
    // util.callbackify(fn)
    // =========================================================================
    if (isUtil && prop->name == "callbackify") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* fn = lastValue;
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), 
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee callbackify = getRuntimeFunction("ts_util_callbackify", ft);
        lastValue = createCall(ft, callbackify.getCallee(), { fn });
        return true;
    }
    
    // =========================================================================
    // util.isDeepStrictEqual(val1, val2)
    // =========================================================================
    if (isUtil && prop->name == "isDeepStrictEqual") {
        if (node->arguments.size() < 2) {
            lastValue = llvm::ConstantInt::get(builder->getInt1Ty(), false);
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* val1 = lastValue;
        visit(node->arguments[1].get());
        llvm::Value* val2 = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(),
            { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_util_is_deep_strict_equal", ft);
        lastValue = createCall(ft, fn.getCallee(), { val1, val2 });
        return true;
    }

    // =========================================================================
    // util.stripVTControlCharacters(str)
    // =========================================================================
    if (isUtil && prop->name == "stripVTControlCharacters") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* str = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_util_strip_vt_control_characters", ft);
        lastValue = createCall(ft, fn.getCallee(), { str });
        return true;
    }

    // =========================================================================
    // util.toUSVString(str)
    // =========================================================================
    if (isUtil && prop->name == "toUSVString") {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
            return true;
        }

        visit(node->arguments[0].get());
        llvm::Value* str = lastValue;

        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = getRuntimeFunction("ts_util_to_usv_string", ft);
        lastValue = createCall(ft, fn.getCallee(), { str });
        return true;
    }

    // =========================================================================
    // util.types.* - Type checking functions
    // =========================================================================
    if (isUtilTypes) {
        if (node->arguments.empty()) {
            lastValue = llvm::ConstantInt::get(builder->getInt1Ty(), false);
            return true;
        }
        
        visit(node->arguments[0].get());
        llvm::Value* value = lastValue;
        
        std::string fnName;
        if (prop->name == "isPromise") fnName = "ts_util_types_is_promise";
        else if (prop->name == "isTypedArray") fnName = "ts_util_types_is_typed_array";
        else if (prop->name == "isArrayBuffer") fnName = "ts_util_types_is_array_buffer";
        else if (prop->name == "isArrayBufferView") fnName = "ts_util_types_is_array_buffer_view";
        else if (prop->name == "isAsyncFunction") fnName = "ts_util_types_is_async_function";
        else if (prop->name == "isDate") fnName = "ts_util_types_is_date";
        else if (prop->name == "isMap") fnName = "ts_util_types_is_map";
        else if (prop->name == "isSet") fnName = "ts_util_types_is_set";
        else if (prop->name == "isRegExp") fnName = "ts_util_types_is_reg_exp";
        else if (prop->name == "isNativeError") fnName = "ts_util_types_is_native_error";
        else if (prop->name == "isUint8Array") fnName = "ts_util_types_is_uint8_array";
        else if (prop->name == "isGeneratorFunction") fnName = "ts_util_types_is_generator_function";
        else if (prop->name == "isGeneratorObject") fnName = "ts_util_types_is_generator_object";
        // Specific TypedArray type checks
        else if (prop->name == "isInt8Array") fnName = "ts_util_types_is_int8_array";
        else if (prop->name == "isInt16Array") fnName = "ts_util_types_is_int16_array";
        else if (prop->name == "isInt32Array") fnName = "ts_util_types_is_int32_array";
        else if (prop->name == "isUint8ClampedArray") fnName = "ts_util_types_is_uint8_clamped_array";
        else if (prop->name == "isUint16Array") fnName = "ts_util_types_is_uint16_array";
        else if (prop->name == "isUint32Array") fnName = "ts_util_types_is_uint32_array";
        else if (prop->name == "isFloat32Array") fnName = "ts_util_types_is_float32_array";
        else if (prop->name == "isFloat64Array") fnName = "ts_util_types_is_float64_array";
        else if (prop->name == "isDataView") fnName = "ts_util_types_is_data_view";
        else {
            // Unknown util.types function - return false
            lastValue = llvm::ConstantInt::get(builder->getInt1Ty(), false);
            return true;
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt1Ty(), 
            { builder->getPtrTy() }, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
        lastValue = createCall(ft, fn.getCallee(), { value });
        return true;
    }
    
    return false;
}

} // namespace ts
