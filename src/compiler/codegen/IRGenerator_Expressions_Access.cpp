#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#include <iostream>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;
void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (node->name == "undefined" || node->name == "null") {
        lastValue = llvm::ConstantPointerNull::get(builder->getPtrTy());
        return;
    }

    if (node->name == "this") {
        llvm::Function* func = builder->GetInsertBlock()->getParent();
        if (func->arg_size() > 1) {
            lastValue = func->getArg(1);
        } else {
            lastValue = func->getArg(0);
        }
        return;
    }

    if (namedValues.count(node->name)) {
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

    // Check for global variable
    llvm::GlobalVariable* gv = module->getGlobalVariable(node->name);
    if (gv) {
        lastValue = builder->CreateLoad(gv->getValueType(), gv, node->name.c_str());
        if (boxedVariables.count(node->name)) {
            boxedValues.insert(lastValue);
        }
        return;
    }

    lastValue = nullptr;
}

void IRGenerator::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    SPDLOG_DEBUG("visitElementAccessExpression");
    if (node->isOptional) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;

        llvm::Value* boxedObj = boxValue(obj, node->expression->inferredType);
        llvm::FunctionType* isNullishFt = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee isNullishFn = module->getOrInsertFunction("ts_value_is_nullish", isNullishFt);
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
        llvm::BasicBlock* finalAccessBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(builder->getPtrTy(), 2);
        phi->addIncoming(undef, checkBB);
        phi->addIncoming(boxValue(accessResult, node->inferredType), finalAccessBB);
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

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Object) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        emitNullCheckForExpression(node->expression.get(), obj);
        visit(node->argumentExpression.get());
        llvm::Value* key = boxValue(lastValue, node->argumentExpression->inferredType);
        
        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
        
        llvm::Value* res = createCall(getFt, getFn.getCallee(), { obj, key });
        // ts_map_get always returns boxed TsValue*
        boxedValues.insert(res);
        lastValue = unboxValue(res, std::make_shared<Type>(TypeKind::Any));
        return;
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Map) {
        visit(node->expression.get());
        llvm::Value* mapVal = lastValue;
        emitNullCheckForExpression(node->expression.get(), mapVal);
        
        visit(node->argumentExpression.get());
        llvm::Value* key = boxValue(lastValue, node->argumentExpression->inferredType);
        
        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
        
        // ts_map_get expects the raw TsMap*
        llvm::Value* rawMap = unboxValue(mapVal, node->expression->inferredType);
        llvm::Value* res = createCall(getFt, getFn.getCallee(), { rawMap, key });
        // ts_map_get always returns boxed TsValue*
        boxedValues.insert(res);
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
                llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int", unboxFt);
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
            llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_buffer_get", getFt);
            llvm::Value* byte = createCall(getFt, getFn.getCallee(), { buf, index });
            lastValue = builder->CreateUIToFP(byte, llvm::Type::getDoubleTy(*context));
            return;
        } else if (cls->name == "Uint8Array" || cls->name == "Uint32Array" || cls->name == "Float64Array") {
            visit(node->expression.get());
            llvm::Value* ta = lastValue;
            emitNullCheckForExpression(node->expression.get(), ta);
            visit(node->argumentExpression.get());
            llvm::Value* index = lastValue;
            index = castValue(index, llvm::Type::getInt64Ty(*context));

            llvm::Type* llvmElemType = nullptr;
            if (cls->name == "Uint8Array") llvmElemType = llvm::Type::getInt8Ty(*context);
            else if (cls->name == "Uint32Array") llvmElemType = llvm::Type::getInt32Ty(*context);
            else if (cls->name == "Float64Array") llvmElemType = llvm::Type::getDoubleTy(*context);

            if (llvmElemType) {
                llvm::StructType* tsTypedArrayType = llvm::StructType::getTypeByName(*context, cls->name);
                if (!tsTypedArrayType) tsTypedArrayType = llvm::StructType::getTypeByName(*context, "TsTypedArray");

                // Bounds check
                if (!isSafe) {
                llvm::Value* lengthPtr = builder->CreateStructGEP(tsTypedArrayType, ta, 3);
                llvm::Value* length = builder->CreateLoad(llvm::Type::getInt64Ty(*context), lengthPtr);
                    emitBoundsCheck(index, length);
                }

                llvm::Value* bufferPtrPtr = builder->CreateStructGEP(tsTypedArrayType, ta, 2);
                llvm::Value* bufferPtr = builder->CreateLoad(builder->getPtrTy(), bufferPtrPtr);
                
                // TsBuffer layout: vtable (0), vtable (1), magic (2), data (3), length (4)
                llvm::StructType* tsBufferType = llvm::StructType::getTypeByName(*context, "TsBuffer");
                llvm::Value* dataPtrPtr = builder->CreateStructGEP(tsBufferType, bufferPtr, 3);
                llvm::Value* dataPtr = builder->CreateLoad(builder->getPtrTy(), dataPtrPtr);
                
                llvm::Value* ptr = builder->CreateGEP(llvmElemType, dataPtr, { index });
                
                lastValue = builder->CreateLoad(llvmElemType, ptr);
                if (cls->name != "Float64Array") {
                    if (node->inferredType && node->inferredType->kind == TypeKind::Int) {
                        lastValue = builder->CreateIntCast(lastValue, llvm::Type::getInt64Ty(*context), false);
                    } else {
                        lastValue = builder->CreateUIToFP(lastValue, llvm::Type::getDoubleTy(*context));
                    }
                }
                return;
            }
            return;
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ArrayType>(node->expression->inferredType);
        auto elemType = arrayType->elementType;

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
            visit(node->expression.get());
            llvm::Value* arr = lastValue;
            emitNullCheckForExpression(node->expression.get(), arr);
            visit(node->argumentExpression.get());
            llvm::Value* index = lastValue;
            if (index->getType()->isDoubleTy()) {
                index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
            }

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
            lastValue = builder->CreateLoad(llvmElemType, ptr);
            if (elemType->kind == TypeKind::Class) {
                concreteTypes[lastValue] = std::static_pointer_cast<ClassType>(elemType).get();
            }
            return;
        }
    }

    visit(node->expression.get());
    llvm::Value* arr = lastValue;
    
    visit(node->argumentExpression.get());
    llvm::Value* index = lastValue;
    
    if (!node->expression->inferredType || node->expression->inferredType->kind == TypeKind::Any) {
        if (index->getType()->isPointerTy()) {
            // String index on any
            llvm::Value* boxedIndex = boxValue(index, node->argumentExpression->inferredType);
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_value_get_property", ft);
            lastValue = createCall(ft, getFn.getCallee(), { arr, boxedIndex });
        } else {
            // Numeric index on any
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
            llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_value_get_element", ft);
            index = castValue(index, llvm::Type::getInt64Ty(*context));
            lastValue = createCall(ft, getFn.getCallee(), { arr, index });
        }
        return;
    }
    
    index = castValue(index, llvm::Type::getInt64Ty(*context));
    
    if (isSafe) {
        // Safe access
    }

    std::string funcName = isSafe ? "ts_array_get_unchecked" : "ts_array_get";
    llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(),
            { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
    llvm::FunctionCallee getFn = module->getOrInsertFunction(funcName, getFt);
            
    llvm::Value* val = createCall(getFt, getFn.getCallee(), { arr, index });

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
        llvm::FunctionCallee isNullishFn = module->getOrInsertFunction("ts_value_is_nullish", isNullishFt);
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
        llvm::BasicBlock* finalAccessBB = builder->GetInsertBlock();
        builder->CreateBr(mergeBB);

        builder->SetInsertPoint(mergeBB);
        llvm::PHINode* phi = builder->CreatePHI(builder->getPtrTy(), 2);
        phi->addIncoming(undef, checkBB);
        phi->addIncoming(boxValue(accessResult, node->inferredType), finalAccessBB);
        boxedValues.insert(phi);
        lastValue = phi;
        return;
    }
    generatePropertyAccess(node);
}

void IRGenerator::generatePropertyAccess(ast::PropertyAccessExpression* node) {
    if (!node->expression->inferredType) {
        SPDLOG_ERROR("visitPropertyAccessExpression: node->expression->inferredType is NULL for {}", node->name);
    }
    SPDLOG_DEBUG("visitPropertyAccessExpression: {} (expr type kind: {})", node->name, 
        node->expression->inferredType ? (int)node->expression->inferredType->kind : -1);

    if (tryGenerateFSPropertyAccess(node)) return;
    if (tryGeneratePathPropertyAccess(node)) return;
    if (tryGenerateHTTPPropertyAccess(node)) return;
    if (tryGenerateNetPropertyAccess(node)) return;

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
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
        llvm::Value* key = createCall(createStrFt, createStrFn.getCallee(), { keyStr });
        
        key = boxValue(key, std::make_shared<Type>(TypeKind::String));

        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
        
        llvm::Value* res = createCall(getFt, getFn.getCallee(), { unboxValue(mapVal, node->expression->inferredType), key });
        // ts_map_get always returns boxed TsValue*
        boxedValues.insert(res);
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
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_value_length", lenFt);
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
            if (cls->name == "Uint8Array" || cls->name == "Uint32Array" || cls->name == "Float64Array") {
                llvm::StructType* tsTypedArrayType = llvm::StructType::getTypeByName(*context, cls->name);
                if (!tsTypedArrayType) tsTypedArrayType = llvm::StructType::getTypeByName(*context, "TsTypedArray");
                
                if (tsTypedArrayType) {
                    llvm::Value* lengthPtr = builder->CreateStructGEP(tsTypedArrayType, obj, 3);
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
            llvm::FunctionCallee getArgvFn = module->getOrInsertFunction("ts_get_process_argv", getArgvFt);
            lastValue = createCall(getArgvFt, getArgvFn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "env") {
            llvm::FunctionType* envFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee envFn = module->getOrInsertFunction("ts_get_process_env", envFt);
            lastValue = unboxValue(createCall(envFt, envFn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "exitCode") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_exit_code", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "platform") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_platform", ft);
            lastValue = unboxValue(createCall(ft, fn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "arch") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_arch", ft);
            lastValue = unboxValue(createCall(ft, fn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "stdout") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_stdout", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "stderr") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_stderr", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "stdin") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_stdin", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        
        // ====================================================================
        // Milestone 102.5: Process Info Properties
        // ====================================================================
        
        if (id->name == "process" && node->name == "pid") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_pid", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "ppid") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_ppid", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "version") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_version", ft);
            lastValue = unboxValue(createCall(ft, fn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "versions") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_versions", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "argv0") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_argv0", ft);
            lastValue = unboxValue(createCall(ft, fn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "execPath") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_exec_path", ft);
            lastValue = unboxValue(createCall(ft, fn.getCallee(), {}), node->inferredType);
            return;
        }
        if (id->name == "process" && node->name == "execArgv") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_exec_argv", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "title") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_title", ft);
            lastValue = unboxValue(createCall(ft, fn.getCallee(), {}), node->inferredType);
            return;
        }
        
        // ====================================================================
        // Milestone 102.10: Configuration & Features Properties
        // ====================================================================
        
        if (id->name == "process" && node->name == "config") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_config", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "features") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_features", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "release") {
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_release", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "debugPort") {
            llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_debug_port", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "process" && node->name == "report") {
            // Return an object with getReport, writeReport, directory, filename
            llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_process_get_report", ft);
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
            }
        } else if (cls->name == "Map" || cls->name == "Set") {
            SPDLOG_DEBUG("Generating {} property: {}", cls->name, node->name);
            visit(node->expression.get());
            llvm::Value* obj = lastValue;
            emitNullCheckForExpression(node->expression.get(), obj);
            
            if (node->name == "size") {
                if (cls->name == "Map") {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_size", ft);
                    lastValue = createCall(ft, fn.getCallee(), { obj });
                } else {
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_set_size", ft);
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
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_text_encoder_get_encoding", ft);
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
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_text_decoder_get_encoding", ft);
                lastValue = unboxValue(createCall(ft, fn.getCallee(), { decoder }), node->inferredType);
                return;
            } else if (node->name == "fatal") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_text_decoder_is_fatal", ft);
                lastValue = createCall(ft, fn.getCallee(), { decoder });
                return;
            } else if (node->name == "ignoreBOM") {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt1Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_text_decoder_ignore_bom", ft);
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_PI", ft);
            lastValue = createCall(ft, fn.getCallee(), {});
            return;
        }
        if (id->name == "Symbol") {
            if (node->name == "asyncIterator") {
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                llvm::Value* tsStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr("[Symbol.asyncIterator]") });
                lastValue = boxValue(tsStr, std::make_shared<Type>(TypeKind::String));
                return;
            } else if (node->name == "iterator") {
                llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
                llvm::Value* tsStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr("[Symbol.iterator]") });
                lastValue = boxValue(tsStr, std::make_shared<Type>(TypeKind::String));
                return;
            }
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(node->expression->inferredType);
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
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_url_search_params_size", ft);
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
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_buffer_byte_length", ft);
                lastValue = createCall(ft, fn.getCallee(), { buf });
                return;
            }
            if (fieldName == "byteOffset") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getInt64Ty(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_buffer_byte_offset", ft);
                lastValue = createCall(ft, fn.getCallee(), { buf });
                return;
            }
            if (fieldName == "buffer") {
                llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
                llvm::FunctionCallee fn = module->getOrInsertFunction("ts_buffer_get_array_buffer", ft);
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
            
            // Load function pointer from VTable
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx);
            
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
                paramTypes.push_back(builder->getPtrTy()); // this
                llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(getterType->returnType), paramTypes, false);
                
                llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(ft), funcPtrPtr);
                lastValue = createCall(ft, funcPtr, { objPtr });
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
            llvm::Value* objPtr = unboxValue(lastValue, node->expression->inferredType);
            emitNullCheckForExpression(node->expression.get(), objPtr);
            
            llvm::Value* keyStr = builder->CreateGlobalStringPtr(node->name);
            
            llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_object_get_property", getFt);
            
            lastValue = createCall(getFt, getFn.getCallee(), { objPtr, keyStr });
            boxedValues.insert(lastValue);
            
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
            
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
            llvm::Value* propNameStr = builder->CreateGlobalStringPtr(node->name);
            llvm::Value* propName = createCall(createStrFt, createStrFn.getCallee(), { propNameStr });
            
            llvm::FunctionType* getPropFt = llvm::FunctionType::get(builder->getPtrTy(), 
                    { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getPropFn = module->getOrInsertFunction("ts_value_get_property", getPropFt);
            
            lastValue = createCall(getPropFt, getPropFn.getCallee(), { objPtr, propName });
            boxedValues.insert(lastValue);
            return;
        }
        
        lastValue = nullptr;
    }
} // namespace ts


