#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;
void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (namedValues.count(node->name)) {
        llvm::Value* val = namedValues[node->name];
        llvm::Type* type = nullptr;
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
            type = alloca->getAllocatedType();
        } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(val)) {
            type = gep->getResultElementType();
        }
        
        if (type) {
            lastValue = builder->CreateLoad(type, val, node->name.c_str());
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
        return;
    }

    lastValue = nullptr;
}

void IRGenerator::visitElementAccessExpression(ast::ElementAccessExpression* node) {
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
        llvm::Value* key = lastValue;
        
        llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
        
        llvm::Value* res = createCall(getFt, getFn.getCallee(), { obj, key });
        lastValue = unboxValue(res, std::make_shared<Type>(TypeKind::Any));
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
                llvm::Value* lengthPtr = builder->CreateStructGEP(tsBufferType, buf, 3);
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

            if (cls->name == "Uint8Array") {
                llvm::FunctionType* getFt = llvm::FunctionType::get(llvm::Type::getInt8Ty(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_typed_array_get_u8", getFt);
                llvm::Value* val = createCall(getFt, getFn.getCallee(), { ta, index });
                lastValue = builder->CreateUIToFP(val, llvm::Type::getDoubleTy(*context));
            } else if (cls->name == "Uint32Array") {
                llvm::FunctionType* getFt = llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_typed_array_get_u32", getFt);
                llvm::Value* val = createCall(getFt, getFn.getCallee(), { ta, index });
                lastValue = builder->CreateUIToFP(val, llvm::Type::getDoubleTy(*context));
            } else if (cls->name == "Float64Array") {
                llvm::FunctionType* getFt = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
                llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_typed_array_get_f64", getFt);
                lastValue = createCall(getFt, getFn.getCallee(), { ta, index });
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

            llvm::FunctionType* getPtrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee getPtrFn = module->getOrInsertFunction("ts_array_get_elements_ptr", getPtrFt);
            llvm::Value* elementsPtr = createCall(getPtrFt, getPtrFn.getCallee(), { arr });

            // Bounds check
            if (!isSafe) {
                llvm::StructType* tsArrayType = llvm::StructType::getTypeByName(*context, "TsArray");
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
            std::string mangledName = cls->name + "_static_" + node->name;
            auto* gVar = module->getGlobalVariable(mangledName);
            if (gVar) {
                lastValue = builder->CreateLoad(gVar->getValueType(), gVar);
                return;
            }

            // Check if it's a static method
            current = cls;
            while (current) {
                if (current->staticMethods.count(node->name)) {
                    std::string implName = current->name + "_static_" + node->name;
                    auto methodType = current->staticMethods[node->name];
                    
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
            for (const auto& f : classLayouts[className].allFields) {
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
             llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length", lenFt);
             lastValue = createCall(lenFt, lenFn.getCallee(), { obj });
        } else if (node->expression->inferredType->kind == TypeKind::Array || node->expression->inferredType->kind == TypeKind::Tuple) {
             llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length", lenFt);
             lastValue = createCall(lenFt, lenFn.getCallee(), { obj });
        } else if (node->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(node->expression->inferredType);
            if (cls->name == "Uint8Array" || cls->name == "Uint32Array" || cls->name == "Float64Array") {
                llvm::FunctionType* lenFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
                llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_typed_array_length", lenFt);
                lastValue = createCall(lenFt, lenFn.getCallee(), { obj });
            } else {
                lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
            }
        } else {
             lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
        }
    } else if (node->name == "size" && node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Map) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        emitNullCheckForExpression(node->expression.get(), obj);
        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, builder->getPtrTy());
        }
        
        llvm::FunctionType* sizeFt = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false);
        llvm::FunctionCallee sizeFn = module->getOrInsertFunction("ts_map_size", sizeFt);
        lastValue = createCall(sizeFt, sizeFn.getCallee(), { obj });
    } else {
        if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Intersection)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            emitNullCheckForExpression(node->expression.get(), objPtr);
            
            llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
            llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(node->name) });
            
            llvm::FunctionType* getFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
            llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get", getFt);
            
            lastValue = createCall(getFt, getFn.getCallee(), { objPtr, keyStr });
            
            // Find the field type to unbox correctly
            std::shared_ptr<Type> fieldType = std::make_shared<Type>(TypeKind::Any);
            if (node->expression->inferredType->kind == TypeKind::Object) {
                auto obj = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
                if (obj->fields.count(node->name)) fieldType = obj->fields[node->name];
            }
            
            lastValue = unboxValue(lastValue, fieldType);
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
            return;
        }
        
        lastValue = nullptr;
    }
}

} // namespace ts


