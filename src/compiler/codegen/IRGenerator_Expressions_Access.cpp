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
    visit(node->expression.get());
    llvm::Value* arr = lastValue;
    
    visit(node->argumentExpression.get());
    llvm::Value* index = lastValue;
    
    if (index->getType()->isDoubleTy()) {
        index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
    }
    
    llvm::FunctionCallee getFn = module->getOrInsertFunction("ts_array_get",
        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
            
    llvm::Value* val = builder->CreateCall(getFn, { arr, index });

    if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->expression->inferredType);
        if (arrayType->elementType->kind == TypeKind::String || arrayType->elementType->kind == TypeKind::Object) {
             lastValue = builder->CreateIntToPtr(val, llvm::PointerType::getUnqual(*context));
             return;
        }
    }
    
    lastValue = val;
}

void IRGenerator::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
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
        if (obj->getType()->isIntegerTy(64)) {
            obj = builder->CreateIntToPtr(obj, llvm::PointerType::getUnqual(*context));
        }
        
        if (!node->expression->inferredType) {
             llvm::errs() << "Warning: No type inferred for length access, assuming string\n";
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
             return;
        }
        
        if (node->expression->inferredType->kind == TypeKind::String) {
             llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(lenFn, { obj });
        } else if (node->expression->inferredType->kind == TypeKind::Array) {
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
            
            std::map<std::string, std::shared_ptr<Type>> allFields;
            if (node->expression->inferredType->kind == TypeKind::Object) {
                allFields = std::static_pointer_cast<ObjectType>(node->expression->inferredType)->fields;
            } else {
                auto inter = std::static_pointer_cast<IntersectionType>(node->expression->inferredType);
                for (auto& t : inter->types) {
                    if (t->kind == TypeKind::Object) {
                        for (auto& [name, type] : std::static_pointer_cast<ObjectType>(t)->fields) {
                            allFields[name] = type;
                        }
                    }
                }
            }
            
            std::vector<llvm::Type*> fieldTypes;
            int fieldIdx = -1;
            int idx = 0;
            for (auto& [name, type] : allFields) {
                fieldTypes.push_back(getLLVMType(type));
                if (name == node->name) {
                    fieldIdx = idx;
                }
                idx++;
            }
            
            if (fieldIdx != -1) {
                llvm::StructType* structType = llvm::StructType::get(*context, fieldTypes);
                llvm::Value* fieldPtr = builder->CreateStructGEP(structType, objPtr, fieldIdx);
                lastValue = builder->CreateLoad(fieldTypes[fieldIdx], fieldPtr, node->name);
                return;
            }
        }

        if (node->expression->inferredType && node->expression->inferredType->kind == TypeKind::Any) {
            visit(node->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create",
                llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::PointerType::getUnqual(*context) }, false));
            llvm::Value* propNameStr = builder->CreateGlobalStringPtr(node->name);
            llvm::Value* propName = builder->CreateCall(createStrFn, { propNameStr });
            
            llvm::FunctionCallee getPropFn = module->getOrInsertFunction("ts_object_get_property",
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


