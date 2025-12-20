#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;
void IRGenerator::visitBinaryExpression(ast::BinaryExpression* node) {
    if (node->op == "instanceof") {
        visit(node->left.get());
        llvm::Value* left = lastValue;
        
        if (auto id = dynamic_cast<ast::Identifier*>(node->right.get())) {
            std::string className = id->name;
            std::string vtableGlobalName = className + "_VTable_Global";
            llvm::GlobalVariable* vtableGlobal = module->getGlobalVariable(vtableGlobalName);
            
            if (!vtableGlobal) {
                lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
                return;
            }
            
            llvm::FunctionCallee instanceofFn = module->getOrInsertFunction("ts_instanceof",
                llvm::Type::getInt1Ty(*context),
                builder->getPtrTy(), builder->getPtrTy());
            
            lastValue = builder->CreateCall(instanceofFn, { left, vtableGlobal });
            return;
        }
        lastValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), 0);
        return;
    }

    visit(node->left.get());
    llvm::Value* left = lastValue;

    visit(node->right.get());
    llvm::Value* right = lastValue;

    if (!left || !right) return;

    // Unbox if one is a pointer and the other is not, and it's not a string operation
    bool leftIsPtr = left->getType()->isPointerTy();
    bool rightIsPtr = right->getType()->isPointerTy();
    
    if (leftIsPtr && !rightIsPtr && node->left->inferredType && 
        node->left->inferredType->kind != TypeKind::String &&
        node->left->inferredType->kind != TypeKind::Null &&
        node->left->inferredType->kind != TypeKind::Undefined) {
        if (right->getType()->isDoubleTy()) {
            left = unboxValue(left, std::make_shared<Type>(TypeKind::Double));
        } else if (right->getType()->isIntegerTy(64)) {
            left = unboxValue(left, std::make_shared<Type>(TypeKind::Int));
        } else if (right->getType()->isIntegerTy(1)) {
            left = unboxValue(left, std::make_shared<Type>(TypeKind::Boolean));
        }
    }
    if (rightIsPtr && !leftIsPtr && node->right->inferredType && 
        node->right->inferredType->kind != TypeKind::String &&
        node->right->inferredType->kind != TypeKind::Null &&
        node->right->inferredType->kind != TypeKind::Undefined) {
        if (left->getType()->isDoubleTy()) {
            right = unboxValue(right, std::make_shared<Type>(TypeKind::Double));
        } else if (left->getType()->isIntegerTy(64)) {
            right = unboxValue(right, std::make_shared<Type>(TypeKind::Int));
        } else if (left->getType()->isIntegerTy(1)) {
            right = unboxValue(right, std::make_shared<Type>(TypeKind::Boolean));
        }
    }

    bool leftIsString = node->left->inferredType && node->left->inferredType->kind == TypeKind::String;
    bool rightIsString = node->right->inferredType && node->right->inferredType->kind == TypeKind::String;
    bool isString = leftIsString || rightIsString;

    if (leftIsPtr && rightIsPtr) {
        // Both are pointers. If it's an arithmetic operation, we MUST unbox.
        // EXCEPT if it's a string concatenation!
        if ((node->op == "+" && !isString) || node->op == "-" || node->op == "*" || node->op == "/" || node->op == "%") {
             left = unboxValue(left, std::make_shared<Type>(TypeKind::Double));
             right = unboxValue(right, std::make_shared<Type>(TypeKind::Double));
        }
    }

    bool leftIsDouble = left->getType()->isDoubleTy();
    bool rightIsDouble = right->getType()->isDoubleTy();
    bool isDouble = leftIsDouble || rightIsDouble;
    
    // bool leftIsString = node->left->inferredType && node->left->inferredType->kind == TypeKind::String;
    // bool rightIsString = node->right->inferredType && node->right->inferredType->kind == TypeKind::String;
    // bool isString = leftIsString || rightIsString;

    if (isDouble && !isString) {
        if (!leftIsDouble) left = builder->CreateSIToFP(left, llvm::Type::getDoubleTy(*context), "casttmp");
        if (!rightIsDouble) right = builder->CreateSIToFP(right, llvm::Type::getDoubleTy(*context), "casttmp");
    }

    if (node->op == "+") {
        if (isString) {
             // Convert non-string operands to string
             if (!leftIsString) {
                 if (left->getType()->isIntegerTy(1)) {
                     llvm::FunctionCallee fromBool = module->getOrInsertFunction("ts_string_from_bool",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt1Ty(*context) }, false));
                     left = builder->CreateCall(fromBool, { left });
                 } else if (left->getType()->isIntegerTy()) {
                     llvm::FunctionCallee fromInt = module->getOrInsertFunction("ts_string_from_int",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false));
                     if (left->getType()->isIntegerTy(32)) {
                         left = builder->CreateSExt(left, llvm::Type::getInt64Ty(*context));
                     }
                     left = builder->CreateCall(fromInt, { left });
                 } else if (left->getType()->isDoubleTy()) {
                     llvm::FunctionCallee fromDouble = module->getOrInsertFunction("ts_string_from_double",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false));
                     left = builder->CreateCall(fromDouble, { left });
                 } else if (left->getType()->isPointerTy()) {
                     // Assume it's a TsValue*
                     llvm::FunctionCallee fromValue = module->getOrInsertFunction("ts_string_from_value",
                         llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                     left = builder->CreateCall(fromValue, { left });
                 }
             }
             if (!rightIsString) {
                 if (right->getType()->isIntegerTy(1)) {
                     llvm::FunctionCallee fromBool = module->getOrInsertFunction("ts_string_from_bool",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt1Ty(*context) }, false));
                     right = builder->CreateCall(fromBool, { right });
                 } else if (right->getType()->isIntegerTy()) {
                     llvm::FunctionCallee fromInt = module->getOrInsertFunction("ts_string_from_int",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false));
                     if (right->getType()->isIntegerTy(32)) {
                         right = builder->CreateSExt(right, llvm::Type::getInt64Ty(*context));
                     }
                     right = builder->CreateCall(fromInt, { right });
                 } else if (right->getType()->isDoubleTy()) {
                     llvm::FunctionCallee fromDouble = module->getOrInsertFunction("ts_string_from_double",
                         llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getDoubleTy(*context) }, false));
                     right = builder->CreateCall(fromDouble, { right });
                 } else if (right->getType()->isPointerTy()) {
                     // Assume it's a TsValue*
                     llvm::FunctionCallee fromValue = module->getOrInsertFunction("ts_string_from_value",
                         llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
                     right = builder->CreateCall(fromValue, { right });
                 }
             }

             llvm::FunctionCallee concatFn = module->getOrInsertFunction("ts_string_concat",
                 llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false));
             lastValue = builder->CreateCall(concatFn, { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFAdd(left, right, "addtmp");
        } else {
            lastValue = builder->CreateAdd(left, right, "addtmp");
        }
    } else if (node->op == "-") {
        if (isDouble) lastValue = builder->CreateFSub(left, right, "subtmp");
        else lastValue = builder->CreateSub(left, right, "subtmp");
    } else if (node->op == "*") {
        if (isDouble) lastValue = builder->CreateFMul(left, right, "multmp");
        else lastValue = builder->CreateMul(left, right, "multmp");
    } else if (node->op == "<") {
        if (isDouble) lastValue = builder->CreateFCmpOLT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLT(left, right, "cmptmp");
    } else if (node->op == "<=") {
        if (isDouble) lastValue = builder->CreateFCmpOLE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSLE(left, right, "cmptmp");
    } else if (node->op == ">") {
        if (isDouble) lastValue = builder->CreateFCmpOGT(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGT(left, right, "cmptmp");
    } else if (node->op == ">=") {
        if (isDouble) lastValue = builder->CreateFCmpOGE(left, right, "cmptmp");
        else lastValue = builder->CreateICmpSGE(left, right, "cmptmp");
    } else if (node->op == "==" || node->op == "===") {
        if (isString) {
             if (!leftIsString) left = unboxValue(left, std::make_shared<Type>(TypeKind::String));
             if (!rightIsString) right = unboxValue(right, std::make_shared<Type>(TypeKind::String));
             
             llvm::FunctionCallee eqFn = module->getOrInsertFunction("ts_string_eq",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(eqFn, { left, right });
        } else if (isDouble) {
            lastValue = builder->CreateFCmpOEQ(left, right, "cmptmp");
        } else {
            lastValue = builder->CreateICmpEQ(left, right, "cmptmp");
        }
    } else if (node->op == "!=" || node->op == "!==") {
        if (isString) {
             if (!leftIsString) left = unboxValue(left, std::make_shared<Type>(TypeKind::String));
             if (!rightIsString) right = unboxValue(right, std::make_shared<Type>(TypeKind::String));

             llvm::FunctionCallee eqFn = module->getOrInsertFunction("ts_string_eq",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             llvm::Value* eq = builder->CreateCall(eqFn, { left, right });
             lastValue = builder->CreateNot(eq);
        } else if (isDouble) {
            lastValue = builder->CreateFCmpONE(left, right, "cmptmp");
        } else {
            lastValue = builder->CreateICmpNE(left, right, "cmptmp");
        }
    } else if (node->op == "&&") {
        lastValue = builder->CreateAnd(left, right, "andtmp");
    } else if (node->op == "||") {
        lastValue = builder->CreateOr(left, right, "ortmp");
    }
}

void IRGenerator::visitAssignmentExpression(ast::AssignmentExpression* node) {
    // 1. Evaluate the RHS
    visit(node->right.get());
    llvm::Value* val = lastValue;
    if (!val) return;

    // 2. Check LHS type
    if (auto id = dynamic_cast<ast::Identifier*>(node->left.get())) {
        // 3. Look up the variable
        llvm::Value* variable = nullptr;
        llvm::Type* varType = nullptr;

        if (namedValues.count(id->name)) {
            variable = namedValues[id->name];
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(variable)) {
                varType = alloca->getAllocatedType();
            } else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(variable)) {
                varType = gep->getResultElementType();
            }
        } else {
            // Check for global variable
            llvm::GlobalVariable* gv = module->getGlobalVariable(id->name);
            if (gv) {
                variable = gv;
                varType = gv->getValueType();
            }
        }

        if (!variable) {
            llvm::errs() << "Error: Unknown variable name " << id->name << "\n";
            return;
        }

        // 4. Store the value
        val = castValue(val, varType);
        builder->CreateStore(val, variable);
    } else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Object) {
            visit(elem->expression.get());
            llvm::Value* obj = lastValue;
            visit(elem->argumentExpression.get());
            llvm::Value* key = boxValue(lastValue, elem->argumentExpression->inferredType);
            
            llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set",
                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
            
            llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
            builder->CreateCall(setFn, { obj, key, boxedVal });
            lastValue = val;
            return;
        }

        if (elem->expression->inferredType && elem->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(elem->expression->inferredType);
            if (cls->name == "Buffer") {
                visit(elem->expression.get());
                llvm::Value* buf = lastValue;
                visit(elem->argumentExpression.get());
                llvm::Value* index = lastValue;
                if (index->getType()->isPointerTy()) {
                    llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int",
                        llvm::Type::getInt64Ty(*context), builder->getPtrTy());
                    index = builder->CreateCall(unboxFn, { index });
                }
                
                llvm::Value* byteVal = val;
                if (byteVal->getType()->isPointerTy()) {
                    llvm::FunctionCallee unboxFn = module->getOrInsertFunction("ts_value_get_int",
                        llvm::Type::getInt64Ty(*context), builder->getPtrTy());
                    byteVal = builder->CreateCall(unboxFn, { byteVal });
                }
                if (byteVal->getType()->isDoubleTy()) {
                    byteVal = builder->CreateFPToSI(byteVal, llvm::Type::getInt64Ty(*context));
                }
                byteVal = builder->CreateTrunc(byteVal, llvm::Type::getInt8Ty(*context));
                
                llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_buffer_set",
                    llvm::Type::getVoidTy(*context), builder->getPtrTy(), llvm::Type::getInt64Ty(*context), llvm::Type::getInt8Ty(*context));
                builder->CreateCall(setFn, { buf, index, byteVal });
                lastValue = val;
                return;
            }
        }

        visit(elem->expression.get());
        llvm::Value* arr = lastValue;
        
        visit(elem->argumentExpression.get());
        llvm::Value* index = lastValue;
        
        if (index->getType()->isDoubleTy()) {
            index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
        }
        
        llvm::Value* storeVal = val;
        if (storeVal->getType()->isDoubleTy()) {
            storeVal = builder->CreateBitCast(storeVal, llvm::Type::getInt64Ty(*context));
        } else if (storeVal->getType()->isPointerTy()) {
            storeVal = builder->CreatePtrToInt(storeVal, llvm::Type::getInt64Ty(*context));
        }

        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_array_set",
            llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                
        builder->CreateCall(setFn, { arr, index, storeVal });
    } else if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get())) {
        if (prop->expression->inferredType && (prop->expression->inferredType->kind == TypeKind::Object || prop->expression->inferredType->kind == TypeKind::Intersection)) {
            visit(prop->expression.get());
            llvm::Value* obj = lastValue;
            
            llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create",
                llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false));
            llvm::Value* key = builder->CreateCall(createStrFn, { builder->CreateGlobalStringPtr(prop->name) });
            
            llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set",
                llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                    { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false));
            
            llvm::Value* boxedVal = boxValue(val, node->right->inferredType);
            builder->CreateCall(setFn, { obj, key, boxedVal });
            lastValue = val;
            return;
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            if (cls->name == "RegExp" && prop->name == "lastIndex") {
                visit(prop->expression.get());
                llvm::Value* re = lastValue;
                
                llvm::FunctionCallee fn = module->getOrInsertFunction("RegExp_set_lastIndex",
                    llvm::FunctionType::get(llvm::Type::getVoidTy(*context), { builder->getPtrTy(), llvm::Type::getInt64Ty(*context) }, false));
                
                llvm::Value* indexVal = val;
                if (indexVal->getType()->isDoubleTy()) {
                    indexVal = builder->CreateFPToSI(indexVal, llvm::Type::getInt64Ty(*context));
                }
                
                builder->CreateCall(fn, { re, indexVal });
                lastValue = val;
                return;
            }
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);

                // Check if it's a static setter
                auto current = cls;
                while (current) {
                    if (current->setters.count(prop->name)) {
                        std::string implName = current->name + "_static_set_" + prop->name;
                        auto methodType = current->setters[prop->name];
                        
                        std::vector<llvm::Type*> paramTypes;
                        paramTypes.push_back(getLLVMType(methodType->paramTypes[0]));
                        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), paramTypes, false);
                        
                        llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                        builder->CreateCall(ft, func.getCallee(), { val });
                        lastValue = val;
                        return;
                    }
                    current = current->baseClass;
                }

                std::string mangledName = cls->name + "_static_" + prop->name;
                auto* gVar = module->getGlobalVariable(mangledName);
                if (gVar) {
                    // Cast if necessary
                    val = castValue(val, gVar->getValueType());
                    builder->CreateStore(val, gVar);
                    lastValue = val;
                    return;
                }
            }
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            std::string className = classType->name;
            std::string fieldName = prop->name;
            
            // Check if it's a setter
            std::string vname = "set_" + fieldName;
            if (classLayouts.count(className) && classLayouts[className].methodIndices.count(vname)) {
                visit(prop->expression.get());
                llvm::Value* objPtr = lastValue;
                
                int methodIdx = classLayouts[className].methodIndices[vname];
                
                // Load VTable pointer
                llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), objPtr);
                
                llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
                if (!vtableStruct) return;
                
                // Load function pointer from VTable
                llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIdx);
                llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(*context), funcPtrPtr);
                
                // Get the setter type
                std::shared_ptr<FunctionType> setterType;
                auto current = classType;
                while (current) {
                    if (current->setters.count(fieldName)) {
                        setterType = current->setters[fieldName];
                        break;
                    }
                    current = current->baseClass;
                }
                
                if (setterType) {
                    std::vector<llvm::Type*> paramTypes;
                    paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
                    paramTypes.push_back(getLLVMType(setterType->paramTypes[0])); // value
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(*context), paramTypes, false);
                    
                    // Cast value if necessary
                    llvm::Value* castVal = val;
                    llvm::Type* expectedType = ft->getParamType(1);
                    if (castVal->getType() != expectedType) {
                        if (expectedType->isDoubleTy() && castVal->getType()->isIntegerTy()) {
                            castVal = builder->CreateSIToFP(castVal, expectedType);
                        } else if (expectedType->isIntegerTy() && castVal->getType()->isDoubleTy()) {
                            castVal = builder->CreateFPToSI(castVal, expectedType);
                        }
                    }

                    builder->CreateCall(ft, funcPtr, { objPtr, castVal });
                    lastValue = val;
                    return;
                }
            }

            // Check if it's a field access
            if (classLayouts.count(className) && classLayouts[className].fieldIndices.count(fieldName)) {
                visit(prop->expression.get());
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
                    val = castValue(val, getLLVMType(fieldType));
                    builder->CreateStore(val, fieldPtr);
                    lastValue = val;
                } else {
                    lastValue = nullptr;
                }
            } else {
                llvm::errs() << "Error: Field " << fieldName << " not found in class " << className << "\n";
            }
        }
    } else {
        llvm::errs() << "Error: LHS of assignment must be an identifier or element access\n";
        return;
    }
    
    // Assignment evaluates to the value
    lastValue = val;
}

} // namespace ts


