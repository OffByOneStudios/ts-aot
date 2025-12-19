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
        return;
    }

    // Check for global variable
    llvm::GlobalVariable* gv = module->getGlobalVariable(node->name);
    if (gv) {
        lastValue = builder->CreateLoad(gv->getValueType(), gv, node->name.c_str());
        return;
    }

    llvm::errs() << "Error: Undefined variable " << node->name << "\n";
    lastValue = nullptr;
}

void IRGenerator::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Object) {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        visit(node->argumentExpression.get());
        llvm::Value* key = boxValue(lastValue, node->argumentExpression->inferredType);
        
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get",
            llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
        
        llvm::Value* res = builder->CreateCall(getFn, { obj, key });
        lastValue = unboxValue(builder->CreateIntToPtr(res, builder->getPtrTy()), std::make_shared<Type>(TypeKind::Any));
        return;
    }

    visit(node->expression.get());
    llvm::Value* arr = lastValue;
    
    visit(node->argumentExpression.get());
    llvm::Value* index = lastValue;
    
    if (index->getType()->isDoubleTy()) {
        index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
    }

    if (!node->expression->inferredType || node->expression->inferredType->kind == TypeKind::Any) {
        llvm::FunctionType* ft = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_value_get_element", ft);
        lastValue = builder->CreateCall(getFn, { arr, index });
        return;
    }
    
    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get",
        llvm::FunctionType::get(builder->getPtrTy(),
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
            
    llvm::Value* val = builder->CreateCall(getFn, { arr, index });

    std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        elementType = std::static_pointer_cast<ArrayType>(node->expression->inferredType)->elementType;
    } else if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Tuple) {
        elementType = std::static_pointer_cast<TupleType>(node->expression->inferredType)->elementTypes[0]; // Simplified
    }

    lastValue = unboxValue(val, elementType);
}

void IRGenerator::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (id->name == "process" && node->name == "argv") {
            llvm::FunctionCallee getArgvFn = module->getOrInsertFunction("ts_get_process_argv",
                llvm::FunctionType::get(builder->getPtrTy(), {}, false));
            lastValue = builder->CreateCall(getArgvFn, {});
            return;
        }
    }

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(node->expression->inferredType);
        if (cls->name == "RegExp") {
            visit(node->expression.get());
            llvm::Value* re = lastValue;
            
            if (node->name == "lastIndex") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_lastIndex",
                    llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
                return;
            } else if (node->name == "source") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_source",
                    llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
                return;
            } else if (node->name == "flags") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_flags",
                    llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
                return;
            } else if (node->name == "global") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_global",
                    llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "sticky") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_sticky",
                    llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "ignoreCase") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_ignoreCase",
                    llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
                lastValue = builder->CreateICmpNE(lastValue, llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), 0));
                return;
            } else if (node->name == "multiline") {
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_get_multiline",
                    llvm::FunctionType::get(llvm::Type::getInt32Ty(*context), { builder->getPtrTy() }, false));
                lastValue = builder->CreateCall(fn, { re });
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
            llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_PI",
                llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), {}, false));
            lastValue = builder->CreateCall(fn);
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
                    lastValue = builder->CreateCall(ft, func.getCallee(), {});
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
        
        // Check if it's a getter
        std::string vname = "get_" + fieldName;
        if (classLayouts.count(className) && classLayouts[className].methodIndices.count(vname)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            int methodIdx = classLayouts[className].methodIndices[vname];
            
            // Load VTable pointer (first field of the struct)
            llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), objPtr);
            
            llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
            if (!vtableStruct) return;
            
            // Load function pointer from VTable
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx);
            llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), funcPtrPtr);
            
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
                paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
                llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(getterType->returnType), paramTypes, false);
                
                lastValue = builder->CreateCall(ft, funcPtr, { objPtr });
                return;
            }
        }

        // Check if it's a field access
        if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
            if (!classStruct) return;
            
            llvm::Value* typedObjPtr = builder->CreateBitCast(objPtr, llvm::PointerType::getUnqual(classStruct));
            
            int fieldIndex = classLayouts[className].fieldIndices[fieldName];
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
            } else {
                lastValue = nullptr;
            }
            return;
        }
    }

    if (node->name == "length") {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        
        if (!node->expression->inferredType || node->expression->inferredType->kind == TypeKind::Any) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_value_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { builder->getPtrTy() }, false));
             lastValue = builder->CreateCall(lenFn, { boxValue(obj, node->expression->inferredType) });
             return;
        }

        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
        }
        
        if (node->expression->inferredType->kind == TypeKind::String) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else if (node->expression->inferredType->kind == TypeKind::Array || node->expression->inferredType->kind == TypeKind::Tuple) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_array_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else {
             llvm::errs() << "Error: length property not supported on type " << node->expression->inferredType->toString() << "\n";
             lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
        }
    } else if (node->name == "size") {
        visit(node->expression.get());
        llvm::Value* obj = lastValue;
        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
        }
        
        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Map) {
             llvm::FunctionCallee sizeFn = module->getOrInsertFunction("ts_map_size",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(sizeFn, { obj });
        } else {
             llvm::errs() << "Error: size property only supported on Map\n";
             lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, 0));
        }
    } else {
        if (node->expression->inferredType && (node->expression->inferredType->kind == TypeKind::Object || node->expression->inferredType->kind == TypeKind::Intersection)) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create",
                llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
            llvm::Value* keyStr = builder->CreateCall(createStrFn, { builder->CreateGlobalStringPtr(node->name) });
            
            llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_map_get",
                llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), { builder->getPtrTy(), builder->getPtrTy() }, false));
            
            llvm::Value* res = builder->CreateCall(getFn, { objPtr, keyStr });
            
            // Find the field type to unbox correctly
            std::shared_ptr<Type> fieldType = std::make_shared<Type>(TypeKind::Any);
            if (node->expression->inferredType->kind == TypeKind::Object) {
                auto obj = std::static_pointer_cast<ObjectType>(node->expression->inferredType);
                if (obj->fields.count(node->name)) fieldType = obj->fields[node->name];
            }
            
            lastValue = unboxValue(builder->CreateIntToPtr(res, builder->getPtrTy()), fieldType);
            return;
        }

        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false));
            llvm::Value* propNameStr = builder->CreateGlobalStringPtr(node->name);
            llvm::Value* propName = builder->CreateCall(createStrFn, { propNameStr });
            
            llvm::FunctionCallee getPropFn = module->getOrInsertFunction("ts_value_get_property",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                    { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
            
            lastValue = builder->CreateCall(getPropFn, { objPtr, propName });
            return;
        }
        
        llvm::errs() << "Error: Unknown property " << node->name << "\n";
        lastValue = nullptr;
    }
}

} // namespace ts


