#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#include <iostream>
#include <filesystem>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;
void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (node->name == "undefined" || node->name == "null") {
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return;
    }

    if (node->name == "Infinity") {
        lastValue = llvm::ConstantFP::getInfinity(builder->getDoubleTy(), false);
        return;
    }

    if (node->name == "NaN") {
        lastValue = llvm::ConstantFP::getNaN(builder->getDoubleTy());
        return;
    }

    // __filename - returns the absolute path of the current source file
    if (node->name == "__filename") {
        std::filesystem::path sourcePath = std::filesystem::absolute(currentSourceFile);
        std::string filename = sourcePath.string();
        // Normalize path separators for cross-platform compatibility
        std::replace(filename.begin(), filename.end(), '\\', '/');

        llvm::Value* strPtr = builder->CreateGlobalStringPtr(filename);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        lastValue = createCall(createStrFt, createStrFn.getCallee(), { strPtr });
        return;
    }

    // __dirname - returns the directory containing the current source file
    if (node->name == "__dirname") {
        std::filesystem::path sourcePath = std::filesystem::absolute(currentSourceFile);
        std::string dirname = sourcePath.parent_path().string();
        // Normalize path separators for cross-platform compatibility
        std::replace(dirname.begin(), dirname.end(), '\\', '/');

        llvm::Value* strPtr = builder->CreateGlobalStringPtr(dirname);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        lastValue = createCall(createStrFt, createStrFn.getCallee(), { strPtr });
        return;
    }

    if (node->name == "this") {
        if (currentAsyncFrame) {
            auto it = currentAsyncFrameMap.find("this");
            if (it != currentAsyncFrameMap.end()) {
                llvm::Value* ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
                lastValue = builder->CreateLoad(currentAsyncFrameType->getElementType(it->second), ptr);
                return;
            }
        }
        // In methods, 'this' is stored in namedValues (set up during function prologue)
        if (namedValues.count("this")) {
            llvm::Value* val = namedValues["this"];
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
                lastValue = builder->CreateLoad(alloca->getAllocatedType(), alloca);
            } else {
                lastValue = val;
            }
            return;
        }
        // Fallback: second argument is 'this' (first is context)
        llvm::Function* func = builder->GetInsertBlock()->getParent();
        if (func->arg_size() >= 2) {
            lastValue = func->getArg(1);
        } else if (func->arg_size() >= 1) {
            lastValue = func->getArg(0);
        } else {
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        return;
    }

    if (currentAsyncFrame) {
        auto it = currentAsyncFrameMap.find(node->name);
        if (it != currentAsyncFrameMap.end()) {
            SPDLOG_DEBUG("visitIdentifier: found {} in async frame at index {}", node->name, it->second);
            llvm::Value* ptr = builder->CreateStructGEP(currentAsyncFrameType, currentAsyncFrame, it->second);
            lastValue = builder->CreateLoad(currentAsyncFrameType->getStructElementType(it->second), ptr);
            return;
        }
    }

    if (namedValues.count(node->name)) {
        if (node->inferredType) {
            SPDLOG_DEBUG("visitIdentifier: {} inferredType kind = {}", node->name, (int)node->inferredType->kind);
        } else {
            SPDLOG_DEBUG("visitIdentifier: {} inferredType is null", node->name);
        }
        // Check if this is a cell variable (captured and mutable)
        if (cellVariables.count(node->name)) {
            llvm::Value* cellAlloca = namedValues[node->name];
            llvm::Value* cell = builder->CreateLoad(builder->getPtrTy(), cellAlloca);
            
            llvm::FunctionType* cellGetFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee cellGetFn = getRuntimeFunction("ts_cell_get", cellGetFt);
            lastValue = createCall(cellGetFt, cellGetFn.getCallee(), { cell });
            
            // The value from ts_cell_get is boxed (TsValue*)
            boxedValues.insert(lastValue);
            SPDLOG_DEBUG("visitIdentifier: {} is a cell variable, got boxed value", node->name);
            return;
        }
        
        llvm::Value* val = namedValues[node->name];
        llvm::Type* type = nullptr;
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
            type = alloca->getAllocatedType();
        } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(val)) {
            type = gep->getResultElementType();
            SPDLOG_DEBUG("Identifier {} is GEP, result type: {}", node->name, (type ? std::to_string(type->getTypeID()) : "NULL"));
        } else {
            SPDLOG_DEBUG("Identifier {} val is not alloca or gep", node->name);
        }
        
        if (type) {
            lastValue = builder->CreateLoad(type, val, node->name.c_str());
            
            if (boxedVariables.count(node->name)) {
                boxedValues.insert(lastValue);
            }
        } else {
            lastValue = val;
        }

        if (concreteTypes.count(val)) {
            concreteTypes[lastValue] = concreteTypes[val];
        }

        return;
    }

    SPDLOG_DEBUG("Identifier {} not found in namedValues", node->name);

    // Check for global variable (first unmangled, then mangled with module hash)
    llvm::GlobalVariable* gv = module->getGlobalVariable(node->name);
    if (!gv) {
        // Check if this is a top-level variable that was mangled with a module hash
        for (const auto& symbol : analyzer->topLevelVariables) {
            if (symbol->name == node->name && !symbol->modulePath.empty()) {
                size_t hash = std::hash<std::string>{}(symbol->modulePath);
                std::string mangledName = symbol->name + "_" + std::to_string(hash);
                gv = module->getGlobalVariable(mangledName);
                if (gv) break;
            }
        }
    }
    if (gv) {
        lastValue = builder->CreateLoad(gv->getValueType(), gv, node->name.c_str());
        if (boxedVariables.count(node->name)) {
            boxedValues.insert(lastValue);
        }
        return;
    }

    // Check if this is a reference to a named function (for first-class function usage)
    // We need to create a boxed function reference that can be called via ts_call_N
    if (node->inferredType && node->inferredType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
        
        // Build the mangled name based on parameter types
        std::vector<std::shared_ptr<Type>> paramTypes = funcType->paramTypes;
        std::string mangledName = Monomorphizer::generateMangledName(node->name, paramTypes, {});
        SPDLOG_DEBUG("visitIdentifier: trying to find function {} with mangledName {}", node->name, mangledName);
        
        llvm::Function* func = module->getFunction(mangledName);
        if (!func) {
            // Try without mangling for functions that aren't monomorphized
            func = module->getFunction(node->name);
        }
        
        // If still not found, try to find any function starting with the name_
        // This handles cases where the function was specialized with different types
        if (!func) {
            std::string prefix = node->name + "_";
            for (auto& fn : module->functions()) {
                if (fn.getName().starts_with(prefix)) {
                    func = &fn;
                    SPDLOG_DEBUG("visitIdentifier: found function {} matching prefix {}", fn.getName().str(), prefix);
                    break;
                }
            }
        }
        
        if (func) {
            // The specialized function may take native types (i64, double, etc.)
            // We need to create a wrapper that takes boxed TsValue* arguments
            llvm::FunctionType* funcFT = func->getFunctionType();
            size_t numParams = funcFT->getNumParams();
            
            SPDLOG_INFO("visitIdentifier: found function {} with {} params", func->getName().str(), numParams);
            
            // Check if the function already takes boxed arguments (all ptr types after context)
            bool needsWrapper = false;
            for (size_t i = 1; i < numParams; ++i) {  // Skip context arg
                if (!funcFT->getParamType(i)->isPointerTy()) {
                    needsWrapper = true;
                    break;
                }
            }

            // ts_call_N expects a TsValue* result for non-native functions. If the specialized
            // function returns a primitive (int/double/bool), we must wrap and box the return.
            if (!funcFT->getReturnType()->isPointerTy()) {
                needsWrapper = true;
            }
            
            SPDLOG_INFO("visitIdentifier: needsWrapper={}", needsWrapper);
            
            llvm::Value* funcToBox = func;
            
            if (needsWrapper) {
                // Create a wrapper function that unboxes args, calls the real function, and boxes the result
                static int wrapperCounter = 0;
                std::string wrapperName = func->getName().str() + "_boxed_wrapper_" + std::to_string(wrapperCounter++);
                
                // Wrapper signature: ptr(ptr context, ptr arg1, ptr arg2, ...)
                std::vector<llvm::Type*> wrapperArgTypes;
                wrapperArgTypes.push_back(builder->getPtrTy()); // context
                for (size_t i = 1; i < numParams; ++i) {
                    wrapperArgTypes.push_back(builder->getPtrTy()); // TsValue* for each arg
                }
                
                llvm::FunctionType* wrapperFT = llvm::FunctionType::get(builder->getPtrTy(), wrapperArgTypes, false);
                llvm::Function* wrapper = llvm::Function::Create(wrapperFT, llvm::Function::InternalLinkage, wrapperName, module.get());
                
                llvm::BasicBlock* oldBB = builder->GetInsertBlock();
                llvm::BasicBlock* wrapperBB = llvm::BasicBlock::Create(*context, "entry", wrapper);
                builder->SetInsertPoint(wrapperBB);
                
                // Prepare call args: unbox each boxed argument to the expected native type
                std::vector<llvm::Value*> callArgs;
                callArgs.push_back(wrapper->getArg(0)); // context passthrough
                
                for (size_t i = 1; i < numParams; ++i) {
                    llvm::Value* boxedArg = wrapper->getArg(i);
                    llvm::Type* expectedType = funcFT->getParamType(i);
                    std::shared_ptr<Type> argType = (i - 1 < funcType->paramTypes.size()) 
                        ? funcType->paramTypes[i - 1] 
                        : std::make_shared<Type>(TypeKind::Any);
                    
                    SPDLOG_INFO("Wrapper unboxing arg {}: argType={}, expectedType={}", 
                               i, argType ? (int)argType->kind : -1, (int)expectedType->getTypeID());
                    
                    llvm::Value* callArg = boxedArg;

                    // If the callee expects a native (non-pointer) value, unbox first.
                    // If it already expects a pointer (e.g. defaulted primitives use TsValue*),
                    // pass through the boxed argument unchanged.
                    if (!expectedType->isPointerTy()) {
                        boxedValues.insert(boxedArg);
                        callArg = unboxValue(boxedArg, argType);
                        SPDLOG_INFO("Wrapper unboxed arg {}: type={}", i, (int)callArg->getType()->getTypeID());

                        // Convert to expected LLVM type if needed
                        if (callArg->getType() != expectedType) {
                            if (expectedType->isIntegerTy(64) && callArg->getType()->isDoubleTy()) {
                                callArg = builder->CreateFPToSI(callArg, expectedType);
                            } else if (expectedType->isDoubleTy() && callArg->getType()->isIntegerTy(64)) {
                                callArg = builder->CreateSIToFP(callArg, expectedType);
                            }
                        }
                    }

                    if (callArg->getType() != expectedType) {
                        callArg = castValue(callArg, expectedType);
                    }

                    callArgs.push_back(callArg);
                }
                
                // Call the real function
                llvm::Value* result = createCall(funcFT, func, callArgs);
                
                // Box the result
                llvm::Value* boxedResult = boxValue(result, funcType->returnType);
                builder->CreateRet(boxedResult);
                
                builder->SetInsertPoint(oldBB);
                funcToBox = wrapper;
            }
            
            // Box the function (or wrapper) as a callable value with null context
            llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee makeFnFn = getRuntimeFunction("ts_value_make_function", makeFnFt);
            lastValue = createCall(makeFnFt, makeFnFn.getCallee(), { funcToBox, llvm::ConstantPointerNull::get(builder->getPtrTy()) });
            boxedValues.insert(lastValue);
            SPDLOG_DEBUG("visitIdentifier: boxed function {} as callable value (wrapper={})", node->name, needsWrapper);
            return;
        }
    }

    // In permissive/untyped JS scenarios we can encounter unresolved identifiers.
    // Returning a null llvm::Value* will later crash codegen when used as an operand.
    // Prefer producing a real JS `undefined` value.
    lastValue = getUndefinedValue();
}

void IRGenerator::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    SPDLOG_DEBUG("visitElementAccessExpression");
    if (node->isOptional) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;

        llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
        llvm::FunctionType* isNullishFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee isNullishFn = getRuntimeFunction("ts_value_is_nullish", isNullishFt);
        llvm::Value* isNullish = createCall(isNullishFt, isNullishFn.getCallee(), { boxedObj });
        llvm::Value* undef = getUndefinedValue();

        llvm::Function* func = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* accessBB = llvm::BasicBlock::Create(*context, "opt_access", func);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "opt_merge", func);

        builder->CreateCondBr(isNullish, mergeBB, accessBB);
        llvm::BasicBlock* checkBB = builder->GetInsertBlock();

        builder->SetInsertPoint(accessBB);
        valueOverrides[node->expression.get()] = obj;
        generateElementAccess(node);
        valueOverrides.erase(node->expression.get());
        
        llvm::Value* accessResult = lastValue;
        llvm::Value* boxedAccessResult = boxValue(accessResult, node->inferredType);
        llvm::BasicBlock* finalAccessBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(builder->getPtrTy(), 2);
        phi->addIncoming(undef, checkBB);
        phi->addIncoming(boxedAccessResult, finalAccessBB);
        boxedValues.insert(phi);
        lastValue = phi;
        return;
    }
    generateElementAccess(node);
}

void IRGenerator::generateElementAccess(ast::ElementAccessExpression* node) {
    // Check if this is a safe access for BCE
    bool isSafe = false;
    if (auto id = dynamic_cast<ast::Identifier*>(node->argumentExpression.get())) {
        if (auto arrId = dynamic_cast<ast::Identifier*>(node->expression.get())) {
            for (auto it = loopStack.rbegin(); it != loopStack.rend(); ++it) {
                if (it->safeIndices.count(id->name) && it->safeIndices.at(id->name) == arrId->name) {
                    isSafe = true;
                    break;
                }
            }
        }
    }

    // Handle enum reverse mapping: Color[0] -> "Red"
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Enum) {
        auto enumType = std::static_pointer_cast<EnumType>(node->expression->inferredType);

        // Check if we have a compile-time constant index
        if (auto lit = dynamic_cast<ast::NumericLiteral*>(node->argumentExpression.get())) {
            int indexValue = (int)lit->value;
            // Find the member name for this value
            for (const auto& [name, value] : enumType->members) {
                if (std::holds_alternative<int>(value) && std::get<int>(value) == indexValue) {
                    // Found the member - return its name as a string
                    llvm::Value* strPtr = builder->CreateGlobalStringPtr(name);
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
                    lastValue = createCall(createStrFt, createStrFn.getCallee(), { strPtr });
                    return;
                }
            }
            // Value not found in enum - return undefined
            lastValue = getUndefinedValue();
            return;
        }

        // Dynamic index - need runtime lookup
        // Build a lookup table at compile time and emit runtime code
        // For now, we'll generate a switch/case or use runtime function
        visit(node->argumentExpression.get());
        llvm::Value* indexVal = lastValue;

        // Convert to int if needed
        if (indexVal->getType()->isDoubleTy()) {
            indexVal = builder->CreateFPToSI(indexVal, builder->getInt64Ty());
        }

        // Build a switch statement for reverse lookup
        llvm::Function* func = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "enum_merge", func);
        llvm::BasicBlock* defaultBB = llvm::BasicBlock::Create(*context, "enum_default", func);

        // Collect only numeric members
        std::vector<std::pair<int, std::string>> numericMembers;
        for (const auto& [name, value] : enumType->members) {
            if (std::holds_alternative<int>(value)) {
                numericMembers.push_back({std::get<int>(value), name});
            }
        }

        llvm::SwitchInst* switchInst = builder->CreateSwitch(indexVal, defaultBB, numericMembers.size());

        // Create blocks for each case
        std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>> caseResults;
        for (const auto& [val, name] : numericMembers) {
            llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(*context, "enum_case_" + std::to_string(val), func);
            switchInst->addCase(llvm::ConstantInt::get(builder->getInt64Ty(), val), caseBB);

            builder->SetInsertPoint(caseBB);
            llvm::Value* strPtr = builder->CreateGlobalStringPtr(name);
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
            llvm::Value* strVal = createCall(createStrFt, createStrFn.getCallee(), { strPtr });
            caseResults.push_back({caseBB, strVal});
            builder->CreateBr(mergeBB);
        }

        // Default case - return undefined
        builder->SetInsertPoint(defaultBB);
        llvm::Value* undefVal = getUndefinedValue();
        builder->CreateBr(mergeBB);

        // Merge block with PHI
        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(builder->getPtrTy(), numericMembers.size() + 1);
        for (const auto& [bb, val] : caseResults) {
            phi->addIncoming(val, bb);
        }
        phi->addIncoming(undefVal, defaultBB);

        lastValue = phi;
        return;
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
        visit(node->expression.get());
        llvm::Value* obj = boxValue(lastValue, node->expression->inferredType);
        visit(node->argumentExpression.get());
        llvm::Value* key = boxValue(lastValue, node->argumentExpression->inferredType);
        
        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = getRuntimeFunction("ts_object_get_dynamic", getFt);
        
        lastValue = createCall(getFt, getFn.getCallee(), { obj, key });
        boxedValues.insert(lastValue);
        return;
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Object) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        emitNullCheckForExpression(node->expression.get(), obj);
        visit(node->argumentExpression.get());
        llvm::Value* key = lastValue;
        
        // CRITICAL: The key might already be boxed (e.g., from for-in loop iteration)
        // OR it might be a raw TsString*. We need to ensure it's always boxed.
        auto keyType = node->argumentExpression->inferredType;
        key = boxValue(key, keyType);
        
        // Unbox object (might be boxed if loaded from global)
        llvm::Value* rawObj = unboxValue(obj, node->expression->inferredType);
        
        // Unbox again to handle boxed values from globals
        if (rawObj->getType()->isPointerTy()) {
            llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
            rawObj = createCall(getObjFt, getObjFn.getCallee(), { rawObj });
        }
        
        // Use inline map operations (objects ARE maps internally)
        llvm::Value* res = emitInlineMapGet(rawObj, key);
        lastValue = res;
        return;
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Map) {
        visit(node->expression.get());
        llvm::Value* mapVal = lastValue;
        emitNullCheckForExpression(node->expression.get(), mapVal);
        
        visit(node->argumentExpression.get());
        llvm::Value* key = lastValue;
        
        // Box the key for map access
        auto keyType = node->argumentExpression->inferredType;
        key = boxValue(key, keyType);
        
        // Unbox the map value
        llvm::Value* rawMap = unboxValue(mapVal, node->expression->inferredType);
        
        // Use inline map operations
        llvm::Value* res = emitInlineMapGet(rawMap, key);
        lastValue = res;
        return;
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        if (cls->name == "Buffer") {
            visit(node->expression.get());
            llvm::Value* buf = lastValue;
            emitNullCheckForExpression(node->expression.get(), buf);
            visit(node->argumentExpression.get());
            llvm::Value* index = lastValue;
            if (index->getType()->isPointerTy()) {
                llvm::FunctionType* unboxFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_int", unboxFt);
                index = createCall(unboxFt, unboxFn.getCallee(), { index });
            }
            
            // Bounds check
            if (!isSafe) {
                llvm::StructType* tsBufferType = llvm::StructType::getTypeByName(*context, "TsBuffer");
                llvm::Value* lengthPtr = builder->CreateStructGEP(tsBufferType, buf, 4);
                llvm::Value* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                emitBoundsCheck(index, length);
            }

            llvm::FunctionType* getFt = llvm::FunctionType::get(llvm::Type::getInt8Ty(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee getFn = getRuntimeFunction("ts_buffer_get", getFt);
            llvm::Value* byte = createCall(getFt, getFn.getCallee(), { buf, index });
            lastValue = builder->CreateUIToFP(byte, llvm::Type::getDoubleTy(*context));
            return;
        } else if (cls->name == "Int8Array" || cls->name == "Uint8Array" || cls->name == "Uint8ClampedArray" ||
                   cls->name == "Int16Array" || cls->name == "Uint16Array" ||
                   cls->name == "Int32Array" || cls->name == "Uint32Array" ||
                   cls->name == "Float32Array" || cls->name == "Float64Array" ||
                   cls->name == "BigInt64Array" || cls->name == "BigUint64Array") {
            // Use runtime function for all TypedArray element access
            visit(node->expression.get());
            llvm::Value* ta = lastValue;
            emitNullCheckForExpression(node->expression.get(), ta);
            visit(node->argumentExpression.get());
            llvm::Value* index = lastValue;
            index = castValue(index, llvm::Type::getInt64Ty(*context));

            // Use ts_typed_array_get_u8 for all uint8 variants (including clamped - read is same)
            // and appropriate functions for other sizes
            std::string funcName;
            llvm::Type* retType;
            bool isSigned = false;

            if (cls->name == "Int8Array" || cls->name == "Uint8Array" || cls->name == "Uint8ClampedArray") {
                funcName = "ts_typed_array_get_u8";
                retType = llvm::Type::getInt8Ty(*context);
                isSigned = (cls->name == "Int8Array");
            } else if (cls->name == "Int16Array" || cls->name == "Uint16Array") {
                funcName = "ts_typed_array_get_i16";  // We'll use same runtime func
                retType = llvm::Type::getInt16Ty(*context);
                isSigned = (cls->name == "Int16Array");
            } else if (cls->name == "Int32Array" || cls->name == "Uint32Array") {
                funcName = "ts_typed_array_get_u32";
                retType = llvm::Type::getInt32Ty(*context);
                isSigned = (cls->name == "Int32Array");
            } else if (cls->name == "Float32Array") {
                funcName = "ts_typed_array_get_f32";
                retType = llvm::Type::getFloatTy(*context);
            } else if (cls->name == "Float64Array") {
                funcName = "ts_typed_array_get_f64";
                retType = llvm::Type::getDoubleTy(*context);
            } else {
                // BigInt types - treat as i64
                funcName = "ts_typed_array_get_i64";
                retType = llvm::Type::getInt64Ty(*context);
                isSigned = (cls->name == "BigInt64Array");
            }

            // Fall back to u8 for now if function doesn't exist yet
            if (funcName == "ts_typed_array_get_i16" || funcName == "ts_typed_array_get_f32" ||
                funcName == "ts_typed_array_get_i64") {
                // Use generic TsTypedArray::Get via helper
                llvm::FunctionType* getFt = llvm::FunctionType::get(
                    llvm::Type::getDoubleTy(*context),
                    { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) },
                    false);
                llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_typed_array_get_generic", getFt);
                lastValue = createCall(getFt, getFn.getCallee(), { ta, index });
                if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
                    lastValue = builder->CreateFPToSI(lastValue, llvm::Type::getInt64Ty(*context));
                }
                return;
            }

            llvm::FunctionType* getFt = llvm::FunctionType::get(retType, { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee getFn = module->getOrInsertFunction(funcName, getFt);
            lastValue = createCall(getFt, getFn.getCallee(), { ta, index });

            // Convert to expected output type
            if (retType != llvm::Type::getDoubleTy(*context)) {
                if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
                    lastValue = isSigned ?
                        builder->CreateSExt(lastValue, llvm::Type::getInt64Ty(*context)) :
                        builder->CreateZExt(lastValue, llvm::Type::getInt64Ty(*context));
                } else {
                    lastValue = isSigned ?
                        builder->CreateSIToFP(lastValue, llvm::Type::getDoubleTy(*context)) :
                        builder->CreateUIToFP(lastValue, llvm::Type::getDoubleTy(*context));
                }
            }
            return;
        }
    }

    // Check if this is a boxed element array (e.g., rest parameters)
    // These arrays have boxed elements even though the type says int[] or double[]
    bool isBoxedElementArray = false;
    std::shared_ptr<Type> effectiveArrayType = node->expression->inferredType;

    // Special case: RegExpMatchArray acts like a string[] array
    if (effectiveArrayType && effectiveArrayType->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(effectiveArrayType);
        if (cls->name == "RegExpMatchArray") {
            // Treat as string[] array for element access
            effectiveArrayType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
        }
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (boxedElementArrayVars.count(id->name)) {
            isBoxedElementArray = true;
        }
        // Check if we have a more specific type from codegen (e.g., monomorphization)
        if (variableTypes.count(id->name)) {
            effectiveArrayType = variableTypes[id->name];
        }
    }

    if (!isBoxedElementArray && effectiveArrayType && effectiveArrayType->kind == TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ArrayType>(effectiveArrayType);
        auto elemType = arrayType->elementType;

        visit(node->expression.get());
        llvm::Value* arr = lastValue;
        
        // ⚠️ CRITICAL: Always call ts_value_get_object for Array types.
        // The array might be boxed (e.g., accessed from a cloned object) but not tracked
        // in boxedValues after being stored to and loaded from an alloca.
        // ts_value_get_object is idempotent for raw pointers.
        if (arr->getType()->isPointerTy()) {
            llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
            arr = createCall(getObjFt, getObjFn.getCallee(), { arr });
        }
        emitNullCheckForExpression(node->expression.get(), arr);
        
        visit(node->argumentExpression.get());
        llvm::Value* index = lastValue;

        // If index is not an integer, use dynamic access
        if (index->getType()->isPointerTy() || index->getType()->isDoubleTy()) {
            // Box the index - use inline boxing for string indices
            llvm::Value* boxedIndex = nullptr;
            auto indexType = node->argumentExpression->inferredType;
            if (indexType && indexType->kind == TypeKind::String && index->getType()->isPointerTy()) {
                // String index - use inline boxing
                llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_string", unboxFt);
                llvm::Value* rawKey = createCall(unboxFt, unboxFn.getCallee(), { index });
                boxedIndex = emitInlineBox(rawKey, 4);  // ValueType::STRING_PTR = 4
            } else {
                boxedIndex = boxValue(index, indexType);
            }
            
            llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getFn = getRuntimeFunction("ts_array_get_dynamic", getFt);
            
            llvm::Value* res = createCall(getFt, getFn.getCallee(), { boxValue(arr, effectiveArrayType), boxedIndex });
            boxedValues.insert(res);
            lastValue = unboxValue(res, elemType);
            return;
        }

        bool isSpecialized = false;
        llvm::Type* llvmElemType = nullptr;

        if (elemType->kind == TypeKind::Double) {
            isSpecialized = true;
            llvmElemType = llvm::Type::getDoubleTy(*context);
        } else if (elemType->kind == TypeKind::Int) {
            isSpecialized = true;
            llvmElemType = llvm::Type::getInt64Ty(*context);
        } else if (elemType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(elemType);
            if (cls->isStruct) {
                isSpecialized = true;
                llvmElemType = llvm::StructType::getTypeByName(*context, cls->name);
            }
        }

        if (isSpecialized) {
            index = castValue(index, llvm::Type::getInt64Ty(*context));

            // ⚠️ RUNTIME CHECK: Arrays may be typed as specialized at compile time
            // but actually be non-specialized at runtime (e.g., from cloneDeep).
            // Check at runtime and fall back to ts_array_get_as_value if needed.
            llvm::FunctionType* isSpecFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee isSpecFn = getRuntimeFunction("ts_array_is_specialized", isSpecFt);
            llvm::Value* isSpec = createCall(isSpecFt, isSpecFn.getCallee(), { arr });
            
            llvm::Function* func = builder->GetInsertBlock()->getParent();
            llvm::BasicBlock* specBB = llvm::BasicBlock::Create(*context, "spec_access", func);
            llvm::BasicBlock* boxedBB = llvm::BasicBlock::Create(*context, "boxed_access", func);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "access_merge", func);
            
            builder->CreateCondBr(isSpec, specBB, boxedBB);
            
            // Specialized path - direct memory access
            builder->SetInsertPoint(specBB);
            llvm::StructType* tsArrayType = llvm::StructType::getTypeByName(*context, "TsArray");
            llvm::Value* elementsPtrPtr = builder->CreateStructGEP(tsArrayType, arr, 1);
            llvm::Value* elementsPtr = builder->CreateLoad(builder->getPtrTy(), elementsPtrPtr);

            // Bounds check
            if (!isSafe) {
                llvm::Value* lengthPtr = builder->CreateStructGEP(tsArrayType, arr, 2);
                llvm::Value* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                emitBoundsCheck(index, length);
            }

            llvm::Value* ptr = builder->CreateGEP(llvmElemType, elementsPtr, { index });
            llvm::Value* specResult = builder->CreateLoad(llvmElemType, ptr);
            llvm::BasicBlock* specEndBB = builder->GetInsertBlock();
            builder->CreateBr(mergeBB);
            
            // Boxed path - use ts_array_get_as_value
            builder->SetInsertPoint(boxedBB);
            llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(),
                    { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee getFn = getRuntimeFunction("ts_array_get_as_value", getFt);
            llvm::Value* boxedVal = createCall(getFt, getFn.getCallee(), { arr, index });
            // Mark as boxed BEFORE unboxing so unboxValue works correctly
            boxedValues.insert(boxedVal);
            // Unbox to get the raw value matching the element type
            llvm::Value* boxedResult = unboxValue(boxedVal, elemType);
            llvm::BasicBlock* boxedEndBB = builder->GetInsertBlock();
            builder->CreateBr(mergeBB);
            
            // Merge
            builder->SetInsertPoint(mergeBB);
            llvm::PHINode* phi = builder->CreatePHI(specResult->getType(), 2);
            phi->addIncoming(specResult, specEndBB);
            phi->addIncoming(boxedResult, boxedEndBB);
            
            lastValue = phi;
            if (elemType->kind == TypeKind::Class) {
                concreteTypes[lastValue] = std::static_pointer_cast<ClassType>(elemType).get();
            }
            return;
        }
    }

    visit(node->expression.get());
    llvm::Value* arr = lastValue;
    
    // ⚠️ CRITICAL: Always call ts_value_get_object for Array types.
    // Same issue as above - arrays from cloned objects may be boxed but not tracked.
    if (arr->getType()->isPointerTy() && node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
        arr = createCall(getObjFt, getObjFn.getCallee(), { arr });
    }
    
    visit(node->argumentExpression.get());
    llvm::Value* index = lastValue;
    
    if (!node->expression->inferredType || node->expression->inferredType->kind == TypeKind::Any) {
        if (index->getType()->isPointerTy()) {
            // String index on any - use inline boxing for performance
            auto indexType = node->argumentExpression->inferredType;
            llvm::Value* boxedIndex = nullptr;
            if (indexType && indexType->kind == TypeKind::String) {
                // Unbox first (idempotent), then inline box
                llvm::FunctionType* unboxFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee unboxFn = getRuntimeFunction("ts_value_get_string", unboxFt);
                llvm::Value* rawKey = createCall(unboxFt, unboxFn.getCallee(), { index });
                boxedIndex = emitInlineBox(rawKey, 4);  // ValueType::STRING_PTR = 4
            } else {
                boxedIndex = boxValue(index, indexType);
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getFn = getRuntimeFunction("ts_value_get_property", ft);
            lastValue = createCall(ft, getFn.getCallee(), { arr, boxedIndex });
        } else {
            // Numeric index on any
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee getFn = getRuntimeFunction("ts_value_get_element", ft);
            index = castValue(index, llvm::Type::getInt64Ty(*context));
            lastValue = createCall(ft, getFn.getCallee(), { arr, index });
        }
        return;
    }
    
    index = castValue(index, llvm::Type::getInt64Ty(*context));
    
    if (isSafe) {
        // Safe access
    }

    // Use inline IR operations to avoid Windows x64 ABI issues
    // emitInlineArrayGet calls __ts_array_get_inline with scalar parameters
    llvm::Value* val = emitInlineArrayGet(arr, index);
    // emitInlineArrayGet returns boxed TsValue* on stack - already marked as boxed

    std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        elementType = std::static_pointer_cast<ArrayType>(node->expression->inferredType)->elementType;
    } else if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Tuple) {
        elementType = std::static_pointer_cast<TupleType>(node->expression->inferredType)->elementTypes[0]; // Simplified
    }

    lastValue = unboxValue(val, elementType);
    if (elementType->kind == TypeKind::Class) {
        concreteTypes[lastValue] = std::static_pointer_cast<ClassType>(elementType).get();
    }
}

void IRGenerator::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    SPDLOG_DEBUG("visitPropertyAccessExpression: {}", node->name);
    if (node->isOptional) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;

        llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
        llvm::FunctionType* isNullishFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee isNullishFn = getRuntimeFunction("ts_value_is_nullish", isNullishFt);
        llvm::Value* isNullish = createCall(isNullishFt, isNullishFn.getCallee(), { boxedObj });
        llvm::Value* undef = getUndefinedValue();

        llvm::Function* func = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock* accessBB = llvm::BasicBlock::Create(*context, "opt_access", func);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "opt_merge", func);

        builder->CreateCondBr(isNullish, mergeBB, accessBB);
        llvm::BasicBlock* checkBB = builder->GetInsertBlock();

        builder->SetInsertPoint(accessBB);
        valueOverrides[node->expression.get()] = obj;
        generatePropertyAccess(node);
        valueOverrides.erase(node->expression.get());
        
        llvm::Value* accessResult = lastValue;
        llvm::Value* boxedAccessResult = boxValue(accessResult, node->inferredType);
        llvm::BasicBlock* finalAccessBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(builder->getPtrTy(), 2);
        phi->addIncoming(undef, checkBB);
        phi->addIncoming(boxedAccessResult, finalAccessBB);
        boxedValues.insert(phi);
        lastValue = phi;
        return;
    }
    generatePropertyAccess(node);
}

void IRGenerator::generatePropertyAccess(ast::PropertyAccessExpression* node) {
    if (!node->expression->inferredType) {
        // Default to any so we still generate runtime property access in slow-path JS.
        node->expression->inferredType = std::make_shared<Type>(TypeKind::Any);
    }
    SPDLOG_DEBUG("visitPropertyAccessExpression: {} (expr type kind: {})", node->name,
        node->expression->inferredType ? (int)node->expression->inferredType->kind : -1);

    if (tryGenerateFSPropertyAccess(node)) return;
    if (tryGeneratePathPropertyAccess(node)) return;
    if (tryGenerateHTTPPropertyAccess(node)) return;
    if (tryGenerateNetPropertyAccess(node)) return;
    if (tryGenerateOSPropertyAccess(node)) return;
    if (tryGenerateOSConstantsPropertyAccess(node)) return;
    if (tryGenerateStreamPropertyAccess(node)) return;

    // Handle enum member access: MyEnum.Member -> constant integer or string
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Enum) {
        auto enumType = std::static_pointer_cast<EnumType>(node->expression->inferredType);
        auto it = enumType->members.find(node->name);
        SPDLOG_INFO("Enum member access: {}.{}, found={}", enumType->name, node->name, (it != enumType->members.end()));
        if (it != enumType->members.end()) {
            if (std::holds_alternative<int>(it->second)) {
                // Numeric enum member
                SPDLOG_INFO("Numeric enum member: {}", std::get<int>(it->second));
                lastValue = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), std::get<int>(it->second));
            } else {
                // String enum member
                const std::string& strValue = std::get<std::string>(it->second);
                SPDLOG_INFO("String enum member: {}", strValue);
                llvm::Value* strPtr = builder->CreateGlobalStringPtr(strValue);
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
                lastValue = createCall(createStrFt, createStrFn.getCallee(), { strPtr });
            }
            return;
        }
        // If member not found, fall through to dynamic access (shouldn't happen if analyzer works)
    } else {
        SPDLOG_INFO("NOT an enum: inferredType={}", node->expression->inferredType ? (int)node->expression->inferredType->kind : -999);
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
        visit(node->expression.get());
        llvm::Value* obj = boxValue(lastValue, node->expression->inferredType);
        
        llvm::Value* keyStr = builder->CreateGlobalStringPtr(node->name);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
        key = boxValue(key, std::make_shared<Type>(TypeKind::String));

        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = getRuntimeFunction("ts_object_get_dynamic", getFt);
        
        lastValue = createCall(getFt, getFn.getCallee(), { obj, key });
        boxedValues.insert(lastValue);
        return;
    }

    // ES2019: Symbol.prototype.description
    if (node->name == "description") {
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Symbol) {
            visit(node->expression.get());
            llvm::Value* sym = lastValue;
            sym = unboxValue(sym, node->expression->inferredType);

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_symbol_get_description", ft);
            lastValue = createCall(ft, fn.getCallee(), { sym });
            return;
        }
    }

    // Check for size property on Map/Set BEFORE treating Map property access as Map.get()
    if (node->name == "size") {
        if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Map || node->expression->inferredType->kind == TypeKind::SetType)) {
            visit(node->expression.get());
            llvm::Value* obj = lastValue;
            // Unbox if the value is boxed (e.g., came from array element access)
            obj = unboxValue(obj, node->expression->inferredType);
            emitNullCheckForExpression(node->expression.get(), obj);
            
            std::string fnName = (node->expression->inferredType->kind == TypeKind::Map) ? "ts_map_size" : "ts_set_size";
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
            lastValue = createCall(ft, fn.getCallee(), { obj });
            return;
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Map) {
        visit(node->expression.get());
        llvm::Value* mapVal = lastValue;
        emitNullCheckForExpression(node->expression.get(), mapVal);
        
        llvm::Value* keyStr = builder->CreateGlobalStringPtr(node->name);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
        
        key = boxValue(key, std::make_shared<Type>(TypeKind::String));
        
        // Unbox map and use inline operations
        llvm::Value* rawMap = unboxValue(mapVal, node->expression->inferredType);
        llvm::Value* res = emitInlineMapGet(rawMap, key);
        lastValue = res;
        return;
    }

    if (node->name == "length") {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        emitNullCheckForExpression(node->expression.get(), obj);

        if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
            lastLengthArray = id->name;
        } else {
            lastLengthArray = "";
        }
        
        if (!node->expression->inferredType || node->expression->inferredType->kind == TypeKind::Any) {
             llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
             llvm::FunctionCallee lenFn = getRuntimeFunction("ts_value_length", lenFt);
             lastValue = createCall(lenFt, lenFn.getCallee(), { boxValue(obj, node->expression->inferredType) });
             return;
        }

        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
        }
        
        if (node->expression->inferredType->kind == TypeKind::String) {
            llvm::StructType* tsStringType = llvm::StructType::getTypeByName(*context, "TsString");
            llvm::Value* lengthPtr = builder->CreateStructGEP(tsStringType, obj, 1);
            llvm::LoadInst* length = builder->CreateLoad(llvm::Type::getInt32Ty(*context), lengthPtr);
            
            // Add range metadata [0, 2^31)
            llvm::Metadata* rangeArgs[] = {
                llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0)),
                llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 2147483647))
            };
            length->setMetadata(llvm::LLVMContext::MD_range, llvm::MDNode::get(*context, rangeArgs));
            
            lastValue = builder->CreateZExt(length, llvm::Type::getInt64Ty(*context));
            return;
        } else if (node->expression->inferredType->kind == TypeKind::Array || node->expression->inferredType->kind == TypeKind::Tuple) {
            // ⚠️ CRITICAL: Always call ts_value_get_object for Array types.
            // The array might be boxed (e.g., accessed from a cloned object) but not tracked
            // in boxedValues. ts_value_get_object is idempotent for raw pointers.
            if (obj->getType()->isPointerTy()) {
                llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
                obj = createCall(getObjFt, getObjFn.getCallee(), { obj });
            }
            llvm::StructType* tsArrayType = llvm::StructType::getTypeByName(*context, "TsArray");
            llvm::Value* lengthPtr = builder->CreateStructGEP(tsArrayType, obj, 2);
            llvm::LoadInst* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
            
            // Add range metadata [0, 2^63)
            llvm::Metadata* rangeArgs[] = {
                llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
                llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 9223372036854775807LL))
            };
            length->setMetadata(llvm::LLVMContext::MD_range, llvm::MDNode::get(*context, rangeArgs));
            
            lastValue = length;
            return;
        } else if (node->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(node->expression->inferredType);
            // Handle all TypedArray types via runtime call
            if (cls->name == "Int8Array" || cls->name == "Uint8Array" || cls->name == "Uint8ClampedArray" ||
                cls->name == "Int16Array" || cls->name == "Uint16Array" ||
                cls->name == "Int32Array" || cls->name == "Uint32Array" ||
                cls->name == "Float32Array" || cls->name == "Float64Array" ||
                cls->name == "BigInt64Array" || cls->name == "BigUint64Array") {
                // Use runtime call for TypedArray length
                llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee lenFn = getRuntimeFunction("ts_typed_array_length", lenFt);
                lastValue = createCall(lenFt, lenFn.getCallee(), { obj });
                return;
            } else if (cls->name == "Buffer") {
                llvm::StructType* tsBufferType = llvm::StructType::getTypeByName(*context, "TsBuffer");
                llvm::Value* lengthPtr = builder->CreateStructGEP(tsBufferType, obj, 4);
                llvm::LoadInst* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                
                // Add range metadata
                llvm::Metadata* rangeArgs[] = {
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
                    llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 9223372036854775807LL))
                };
                length->setMetadata(llvm::LLVMContext::MD_range, llvm::MDNode::get(*context, rangeArgs));
                
                lastValue = length;
                return;
            }
        }
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "process" && node->name == "argv") {
            llvm::FunctionType* getArgvFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee getArgvFn = getRuntimeFunction("ts_get_process_argv", getArgvFt);
            lastValue = createCall(getArgvFt, getArgvFn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "env") {
            llvm::FunctionType* envFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee envFn = getRuntimeFunction("ts_get_process_env", envFt);
            lastValue = unboxValue(createCall(envFt, envFn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "exitCode") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_exit_code", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "platform") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_platform", ft);
            llvm::Value* boxed = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(boxed);
            lastValue = unboxValue(boxed, node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "arch") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_arch", ft);
            llvm::Value* boxed = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(boxed);
            lastValue = unboxValue(boxed, node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "stdout") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_stdout", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "stderr") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_stderr", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "stdin") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_stdin", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        
        // ====================================================================
        // Milestone 102.5: Process Info Properties
        // ====================================================================
        
        if (id->name == "process" && node->name == "pid") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_pid", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "ppid") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_ppid", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "version") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_version", ft);
            llvm::Value* boxed = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(boxed);
            lastValue = unboxValue(boxed, node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "versions") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_versions", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "argv0") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_argv0", ft);
            llvm::Value* boxed = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(boxed);
            lastValue = unboxValue(boxed, node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "execPath") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_exec_path", ft);
            llvm::Value* boxed = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(boxed);
            lastValue = unboxValue(boxed, node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "execArgv") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_exec_argv", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "title") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_title", ft);
            llvm::Value* boxed = createCall(ft, fn.getCallee(), {});
            boxedValues.insert(boxed);
            lastValue = unboxValue(boxed, node->inferredType);
            return;
        }
        
        // ====================================================================
        // Milestone 102.10: Configuration & Features Properties
        // ====================================================================
        
        if (id->name == "process" && node->name == "config") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_config", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "features") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_features", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "release") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_release", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "debugPort") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_debug_port", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "report") {
            // Return an object with getReport, writeReport, directory, filename
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_process_get_report", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        if (cls->name == "RegExp") {
            visit(node->expression.get());
            llvm::Value* re = lastValue;
            emitNullCheckForExpression(node->expression.get(), re);
            
            if (node->name == "lastIndex") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_lastIndex", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                return;
            } else if (node->name == "source") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_source", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                return;
            } else if (node->name == "flags") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_flags", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                return;
            } else if (node->name == "global") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_global", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "sticky") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_sticky", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "ignoreCase") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_ignoreCase", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "multiline") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_multiline", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "hasIndices") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_hasIndices", ft);
                lastValue = createCall(ft, fn.getCallee(), { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            }
        } else if (cls->name == "RegExpMatchArray") {
            // RegExpMatchArray - result of RegExp.exec()
            visit(node->expression.get());
            llvm::Value* match = lastValue;
            emitNullCheckForExpression(node->expression.get(), match);

            // Use ts_object_get_property for all properties
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(node->name);
            llvm::FunctionType* getPropFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getPropFn = getRuntimeFunction("ts_object_get_property", getPropFt);
            llvm::Value* result = createCall(getPropFt, getPropFn.getCallee(), { match, keyStr });
            boxedValues.insert(result);
            lastValue = unboxValue(result, node->inferredType);
            return;
        } else if (cls->name == "Map" || cls->name == "Set") {
            SPDLOG_DEBUG("Generating {} property: {}", cls->name, node->name);
            visit(node->expression.get());
            llvm::Value* obj = lastValue;
            emitNullCheckForExpression(node->expression.get(), obj);
            
            if (node->name == "size") {
                if (cls->name == "Map") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_map_size", ft);
                    lastValue = createCall(ft, fn.getCallee(), { obj });
                } else {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = getRuntimeFunction("ts_set_size", ft);
                    lastValue = createCall(ft, fn.getCallee(), { obj });
                }
                return;
            }
        } else if (cls->name == "TextEncoder") {
            // TextEncoder property access
            visit(node->expression.get());
            llvm::Value* encoder = lastValue;
            emitNullCheckForExpression(node->expression.get(), encoder);
            
            if (node->name == "encoding") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_text_encoder_get_encoding", ft);
                lastValue = unboxValue(createCall(ft, fn.getCallee(), { encoder }), node->inferredType);
                return;
            }
        } else if (cls->name == "TextDecoder") {
            // TextDecoder property access
            visit(node->expression.get());
            llvm::Value* decoder = lastValue;
            emitNullCheckForExpression(node->expression.get(), decoder);
            
            if (node->name == "encoding") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_text_decoder_get_encoding", ft);
                lastValue = unboxValue(createCall(ft, fn.getCallee(), { decoder }), node->inferredType);
                return;
            } else if (node->name == "fatal") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_text_decoder_is_fatal", ft);
                lastValue = createCall(ft, fn.getCallee(), { decoder });
                return;
            } else if (node->name == "ignoreBOM") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_text_decoder_ignore_bom", ft);
                lastValue = createCall(ft, fn.getCallee(), { decoder });
                return;
            }
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Namespace) {
        // Namespace property access: math.x
        // This refers to a global variable in another module
        // For now, we assume global variables are not mangled by module
        auto* gVar = module->getGlobalVariable(node->name);
        if (gVar) {
            lastValue = builder->CreateLoad(gVar->getValueType(), gVar);
        } else {
            // If not found, it might be a function reference (not a call)
            // TODO: Handle function references
            lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        }
        return;
    }

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "Math" && node->name == "PI") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false);
            llvm::FunctionCallee fn = getRuntimeFunction("ts_math_PI", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "Symbol") {
            // Handle all well-known symbols - they return a string "[Symbol.NAME]"
            // which can be used as a property key
            static const std::unordered_map<std::string, std::string> wellKnownSymbols = {
                {"iterator", "[Symbol.iterator]"},
                {"asyncIterator", "[Symbol.asyncIterator]"},
                {"hasInstance", "[Symbol.hasInstance]"},
                {"isConcatSpreadable", "[Symbol.isConcatSpreadable]"},
                {"match", "[Symbol.match]"},
                {"matchAll", "[Symbol.matchAll]"},
                {"replace", "[Symbol.replace]"},
                {"search", "[Symbol.search]"},
                {"split", "[Symbol.split]"},
                {"species", "[Symbol.species]"},
                {"toPrimitive", "[Symbol.toPrimitive]"},
                {"toStringTag", "[Symbol.toStringTag]"},
                {"unscopables", "[Symbol.unscopables]"},
            };

            auto it = wellKnownSymbols.find(node->name);
            if (it != wellKnownSymbols.end()) {
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
                llvm::Value* tsStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(it->second) });
                lastValue = boxValue(tsStr, std::make_shared<Type>(TypeKind::String));
                return;
            }
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(node->expression->inferredType);
        if (node->name == "call" || node->name == "apply") {
            // Treat foo.call/apply as direct reference to foo; we don't model Function.prototype here.
            visit(node->expression.get());
            lastValue = boxValue(lastValue, node->expression->inferredType);
            boxedValues.insert(lastValue);
            return;
        }
        if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            
            // Check if it's a static getter
            auto current = cls;
            while (current) {
                if (current->getters.count(node->name)) {
                    std::string implName = current->name + "_static_get_" + node->name;
                    auto methodType = current->getters[node->name];
                    
                    std::vector<llvm::Type*> paramTypes;
                    llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);
                    
                    llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                    lastValue = createCall(ft, func.getCallee(), {});
                    return;
                }
                current = current->baseClass;
            }

            // Check if it's a static field
            std::string fieldName = node->name;
            if (fieldName.starts_with("#")) {
                fieldName = manglePrivateName(fieldName, cls->name);
            }
            std::string mangledName = cls->name + "_" + fieldName;
            SPDLOG_DEBUG("Looking up static field: {} in class {}", mangledName, cls->name);
            auto* gVar = module->getGlobalVariable(mangledName);
            if (gVar) {
                SPDLOG_DEBUG("Found static field: {}", mangledName);
                lastValue = builder->CreateLoad(gVar->getValueType(), gVar);
                return;
            } else {
                SPDLOG_DEBUG("Static field not found: {}", mangledName);
            }

            // Check if it's a static method
            current = cls;
            while (current) {
                std::string methodName = node->name;
                if (methodName.starts_with("#")) {
                    methodName = manglePrivateName(methodName, current->name);
                }
                if (current->staticMethods.count(methodName)) {
                    std::string implName = current->name + "_static_" + methodName;
                    auto methodType = current->staticMethods[methodName];
                    
                    std::vector<llvm::Type*> paramTypes;
                    for (const auto& param : methodType->paramTypes) {
                        paramTypes.push_back(getLLVMType(param));
                    }
                    llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);
                    
                    lastValue = module->getOrInsertFunction(implName, ft).getCallee();
                    return;
                }
                current = current->baseClass;
            }
        }

        // Generic function property access (fallback)
        visit(node->expression.get());
        llvm::Value* funcVal = boxValue(lastValue, node->expression->inferredType);

        llvm::Value* keyStr = builder->CreateGlobalStringPtr(node->name);
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
        key = boxValue(key, std::make_shared<Type>(TypeKind::String));

        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = getRuntimeFunction("ts_object_get_dynamic", getFt);
        
        lastValue = createCall(getFt, getFn.getCallee(), { funcVal, key });
        boxedValues.insert(lastValue);
        return;
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        std::string className = classType->name;
        std::string fieldName = node->name;

        if (fieldName.starts_with("#")) {
            if (currentClass && currentClass->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(currentClass);
                std::string baseName = cls->originalName.empty() ? cls->name : cls->originalName;
                fieldName = manglePrivateName(fieldName, baseName);
            }
        }
        
        SPDLOG_DEBUG("Accessing {}.{}", className, fieldName);
        
        if (className == "URL") {
            visit(node->expression.get());
            llvm::Value* url = lastValue;
            emitNullCheckForExpression(node->expression.get(), url);
            std::string getterName = "URL_get_" + fieldName;
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(getterName, ft);
            lastValue = createCall(ft, fn.getCallee(), { url });
            return;
        }

        if (className == "URLSearchParams") {
            visit(node->expression.get());
            llvm::Value* params = lastValue;
            emitNullCheckForExpression(node->expression.get(), params);
            if (fieldName == "size") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_url_search_params_size", ft);
                lastValue = createCall(ft, fn.getCallee(), { params });
                return;
            }
        }

        if (className == "IncomingMessage") {
            visit(node->expression.get());
            llvm::Value* msg = lastValue;
            emitNullCheckForExpression(node->expression.get(), msg);
            std::string getterName = "ts_incoming_message_" + fieldName;
            
            llvm::Value* contextVal = currentAsyncContext;
            if (!contextVal) contextVal = llvm::ConstantPointerNull::get(builder->getPtrTy());

            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction(getterName, ft);
            lastValue = createCall(ft, fn.getCallee(), { contextVal, msg });
            return;
        }

        if (className == "Buffer") {
            visit(node->expression.get());
            llvm::Value* buf = lastValue;
            emitNullCheckForExpression(node->expression.get(), buf);
            if (fieldName == "length") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("Buffer_get_length", ft);
                lastValue = createCall(ft, fn.getCallee(), { buf });
                return;
            }
            if (fieldName == "byteLength") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_byte_length", ft);
                lastValue = createCall(ft, fn.getCallee(), { buf });
                return;
            }
            if (fieldName == "byteOffset") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_byte_offset", ft);
                lastValue = createCall(ft, fn.getCallee(), { buf });
                return;
            }
            if (fieldName == "buffer") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = getRuntimeFunction("ts_buffer_get_array_buffer", ft);
                lastValue = createCall(ft, fn.getCallee(), { buf });
                return;
            }
        }

        if (className == "Response") {
            visit(node->expression.get());
            llvm::Value* resp = lastValue;
            emitNullCheckForExpression(node->expression.get(), resp);
            if (fieldName == "status") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("Response_get_status", ft);
                lastValue = createCall(ft, fn.getCallee(), { resp });
                return;
            } else {
                std::string getterName = "Response_get_" + fieldName;
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction(getterName, ft);
                lastValue = createCall(ft, fn.getCallee(), { resp });
                return;
            }
        }

        // ES2022: Error class - stored as TsMap wrapped in TsValue*, use dynamic property access
        if (className == "Error") {
            visit(node->expression.get());
            llvm::Value* err = lastValue;
            emitNullCheckForExpression(node->expression.get(), err);

            // Error objects are boxed TsValue* containing a TsMap, use ts_object_get_dynamic
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(fieldName);
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
            llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
            key = boxValue(key, std::make_shared<Type>(TypeKind::String));

            llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getFn = getRuntimeFunction("ts_object_get_dynamic", getFt);

            lastValue = createCall(getFt, getFn.getCallee(), { err, key });
            boxedValues.insert(lastValue);
            return;
        }

        // Check if it's a getter
        std::string vname = "get_" + fieldName;
        if (classLayouts.count(className) && classLayouts[className].methodIndices.count(vname)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            emitNullCheckForExpression(node->expression.get(), objPtr);
            
            int methodIdx = classLayouts[className].methodIndices[vname];

            // Load VTable pointer (first field of the struct)
            llvm::Value* vptr = builder->CreateLoad(builder->getPtrTy(), objPtr);

            llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
            if (!vtableStruct) return;

            // Load function pointer from VTable (methodIdx + 1 because slot 0 is parent VTable)
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx + 1);
            
            // Get the getter type
            std::shared_ptr<FunctionType> getterType;
            auto currentClass = classType;
            while (currentClass) {
                if (currentClass->getters.count(fieldName)) {
                    getterType = currentClass->getters[fieldName];
                    break;
                }
                currentClass = currentClass->baseClass;
            }
            
            if (getterType) {
                std::vector<llvm::Type*> paramTypes;
                paramTypes.push_back(builder->getPtrTy()); // context
                paramTypes.push_back(builder->getPtrTy()); // this
                llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(getterType->returnType), paramTypes, false);

                llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(ft), funcPtrPtr);
                // Pass context (null or currentAsyncContext) and this
                llvm::Value* contextArg = currentAsyncContext ? currentAsyncContext : llvm::ConstantPointerNull::get(builder->getPtrTy());
                lastValue = createCall(ft, funcPtr, { contextArg, objPtr });
                return;
            }
        }

        // Check if it's a field access
        if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
            SPDLOG_DEBUG("Field access {}.{} index={}", className, fieldName, classLayouts[className].fieldIndices[fieldName]);
            visit(node->expression.get());
            llvm::Value* obj = lastValue;
            emitNullCheckForExpression(node->expression.get(), obj);
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
            if (!classStruct) return;

            int fieldIndex = classLayouts[className].fieldIndices[fieldName];

            if (classType->isStruct && !obj->getType()->isPointerTy()) {
                lastValue = builder->CreateExtractValue(obj, { (unsigned)fieldIndex }, fieldName.c_str());
                return;
            }
            
            llvm::Value* typedObjPtr = builder->CreateBitCast(obj, llvm::PointerType::getUnqual(classStruct));
            llvm::Value* fieldPtr = builder->CreateStructGEP(classStruct, typedObjPtr, fieldIndex);
            
            // We need the type of the field to load it correctly
            // The field could be in a base class, so we look it up in the layout's allFields
            std::shared_ptr<Type> fieldType;
            SPDLOG_DEBUG("Searching for field {} in {} (fields: {})", fieldName, className, classLayouts[className].allFields.size());
            for (const auto& f : classLayouts[className].allFields) {
                SPDLOG_DEBUG("Checking field {}", f.first);
                if (f.first == fieldName) {
                    fieldType = f.second;
                    break;
                }
            }
            
            if (fieldType) {
                lastValue = builder->CreateLoad(getLLVMType(fieldType), fieldPtr);
                
                if (fieldType->kind == TypeKind::Class) {
                    concreteTypes[lastValue] = std::static_pointer_cast<ClassType>(fieldType).get();
                }
            } else {
                lastValue = nullptr;
            }
            return;
        }
    }

    if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Intersection)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            // ⚠️ CRITICAL: Always call ts_value_get_object for Object types.
            // The value might be boxed (e.g., returned from a generic function) but not tracked
            // in boxedValues after being stored to and loaded from an alloca.
            // ts_value_get_object is idempotent for raw pointers.
            if (objPtr->getType()->isPointerTy()) {
                llvm::FunctionType* getObjFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee getObjFn = getRuntimeFunction("ts_value_get_object", getObjFt);
                objPtr = createCall(getObjFt, getObjFn.getCallee(), { objPtr });
            }
            emitNullCheckForExpression(node->expression.get(), objPtr);
            
            // Check if this property has a getter
            if (node->expression->inferredType->kind == TypeKind::Object) {
                auto obj = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
                if (obj->getters.count(node->name)) {
                    // Property has a getter - look up the getter function and call it
                    llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
                    std::string getterKey = "__getter_" + node->name;
                    llvm::Value* keyStr = builder->CreateGlobalStringPtr(getterKey);
                    llvm::Value* keyStrPtr = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
                    llvm::Value* keyBoxed = boxValue(keyStrPtr, std::make_shared<Type>(TypeKind::String));
                    
                    // Get the getter function from the map
                    llvm::Value* getterVal = emitInlineMapGet(objPtr, keyBoxed);
                    
                    // Unbox to get raw function pointer
                    llvm::FunctionType* getFnPtrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee getFnPtrFn = getRuntimeFunction("ts_value_get_function", getFnPtrFt);
                    llvm::Value* getterFn = createCall(getFnPtrFt, getFnPtrFn.getCallee(), { getterVal });
                    
                    // For getter/setter, context is the object itself (this), not the closure context
                    llvm::Value* context = objPtr;
                    
                    // Call the getter: (context) -> TsValue*
                    llvm::FunctionType* getterType = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                    lastValue = createCall(getterType, getterFn, { context });
                    boxedValues.insert(lastValue);
                    
                    // Unbox based on getter return type
                    auto getterFuncType = obj->getters[node->name];
                    if (getterFuncType->returnType && 
                        getterFuncType->returnType->kind != TypeKind::Object && 
                        getterFuncType->returnType->kind != TypeKind::Any) {
                        lastValue = unboxValue(lastValue, getterFuncType->returnType);
                    }
                    return;
                }
            }
            
            // For typed objects, objPtr is already the raw TsMap* - use it directly for map operations
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(node->name);
            llvm::Value* keyStrPtr = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
            llvm::Value* keyBoxed = boxValue(keyStrPtr, std::make_shared<Type>(TypeKind::String));
            
            // Use map operations directly on the TsMap* (objects ARE maps internally)
            lastValue = emitInlineMapGet(objPtr, keyBoxed);
            // emitInlineMapGet returns boxed TsValue* on stack - already marked as boxed
            
            // Find the field type to unbox correctly
            std::shared_ptr<Type> fieldType = std::make_shared<Type>(TypeKind::Any);
            if (node->expression->inferredType->kind == TypeKind::Object) {
                auto obj = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
                if (obj->fields.count(node->name)) fieldType = obj->fields[node->name];
            }
            
            if (fieldType->kind != TypeKind::Object && fieldType->kind != TypeKind::Any && fieldType->kind != TypeKind::Intersection && fieldType->kind != TypeKind::Function) {
                lastValue = unboxValue(lastValue, fieldType);
            }
            return;
        }

        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            emitNullCheckForExpression(node->expression.get(), objPtr);
            
            // Use inline IR operations for any-typed object property access
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
            llvm::Value* propNameStr = builder->CreateGlobalStringPtr(node->name);
            llvm::Value* propName = createCall(createStrFt, createStrFn.getCallee(), { propNameStr });
            llvm::Value* propNameBoxed = boxValue(propName, std::make_shared<Type>(TypeKind::String));
            
            lastValue = emitInlineObjectGetProp(objPtr, propNameBoxed);
            // emitInlineObjectGetProp returns boxed TsValue* - already marked as boxed
            return;
        }
        
        lastValue = nullptr;
    }
} // namespace ts


