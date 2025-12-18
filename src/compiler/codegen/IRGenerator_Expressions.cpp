#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {

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

    bool leftIsDouble = left->getType()->isDoubleTy();
    bool rightIsDouble = right->getType()->isDoubleTy();
    bool isDouble = leftIsDouble || rightIsDouble;
    bool leftIsString = left->getType()->isPointerTy();
    bool rightIsString = right->getType()->isPointerTy();
    bool isString = leftIsString || rightIsString;

    if (isDouble && !isString) {
        if (!leftIsDouble) left = builder->CreateSIToFP(left, llvm::Type::getDoubleTy(*context), "casttmp");
        if (!rightIsDouble) right = builder->CreateSIToFP(right, llvm::Type::getDoubleTy(*context), "casttmp");
    }

    if (node->op == "+") {
        if (isString) {
             // Convert non-string operands to string
             if (!leftIsString) {
                 if (left->getType()->isIntegerTy()) {
                     llvm::FunctionCallee fromInt = module->getOrInsertFunction("ts_string_from_int",
                         llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false));
                     left = builder->CreateCall(fromInt, { left });
                 } else if (left->getType()->isDoubleTy()) {
                     llvm::FunctionCallee fromDouble = module->getOrInsertFunction("ts_string_from_double",
                         llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false));
                     left = builder->CreateCall(fromDouble, { left });
                 }
             }
             if (!rightIsString) {
                 if (right->getType()->isIntegerTy()) {
                     llvm::FunctionCallee fromInt = module->getOrInsertFunction("ts_string_from_int",
                         llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false));
                     right = builder->CreateCall(fromInt, { right });
                 } else if (right->getType()->isDoubleTy()) {
                     llvm::FunctionCallee fromDouble = module->getOrInsertFunction("ts_string_from_double",
                         llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false));
                     right = builder->CreateCall(fromDouble, { right });
                 }
             }

             llvm::Function* concatFn = module->getFunction("ts_string_concat");
             if (!concatFn) {
                 std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) };
                 llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
                 concatFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_concat", module.get());
             }
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

void IRGenerator::visitIdentifier(ast::Identifier* node) {
    if (namedValues.count(node->name)) {
        llvm::Value* val = namedValues[node->name];
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
            lastValue = builder->CreateLoad(alloca->getAllocatedType(), alloca, node->name.c_str());
        } else {
            llvm::errs() << "Error: Variable " << node->name << " is not an alloca\n";
            lastValue = nullptr;
        }
    } else {
        llvm::errs() << "Error: Undefined variable " << node->name << "\n";
        lastValue = nullptr;
    }
}

void IRGenerator::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int64_t)node->value) {
        lastValue = llvm::ConstantInt::get(*context, llvm::APInt(64, (int64_t)node->value));
    } else {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node->value));
    }
}

void IRGenerator::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastValue = llvm::ConstantInt::get(*context, llvm::APInt(1, node->value ? 1 : 0));
}

void IRGenerator::visitStringLiteral(ast::StringLiteral* node) {
    llvm::Function* createFn = module->getFunction("ts_string_create");
    if (!createFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            args, false);
        createFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_create", module.get());
    }

    llvm::Constant* strConst = builder->CreateGlobalStringPtr(node->value);
    lastValue = builder->CreateCall(createFn, { strConst });
}

void IRGenerator::visitCallExpression(ast::CallExpression* node) {
    if (auto superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get())) {
        // super() call in constructor
        if (!currentClass || currentClass->kind != TypeKind::Class) return;
        auto classType = std::static_pointer_cast<ClassType>(currentClass);
        if (!classType->baseClass) return;
        
        std::string baseClassName = classType->baseClass->name;
        std::string ctorName = baseClassName + "_constructor";
        llvm::Function* ctor = module->getFunction(ctorName);
        if (ctor) {
            std::vector<llvm::Value*> args;
            // 'this' is the first argument of the current function
            llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
            args.push_back(currentFunc->getArg(0));
            
            for (auto& arg : node->arguments) {
                visit(arg.get());
                args.push_back(lastValue);
            }
            builder->CreateCall(ctor, args);
        }
        return;
    }

    if (auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get())) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
                
                // Static method call
                auto current = cls;
                while (current) {
                    if (current->staticMethods.count(prop->name)) {
                        std::string implName = current->name + "_static_" + prop->name;
                        auto methodType = current->staticMethods[prop->name];
                        
                        std::vector<llvm::Type*> paramTypes;
                        for (const auto& param : methodType->paramTypes) {
                            paramTypes.push_back(getLLVMType(param));
                        }
                        llvm::FunctionType* ft = llvm::FunctionType::get(
                            getLLVMType(methodType->returnType), paramTypes, false);
                        
                        llvm::FunctionCallee func = module->getOrInsertFunction(implName, ft);
                        
                        std::vector<llvm::Value*> args;
                        int argIdx = 0;
                        for (auto& arg : node->arguments) {
                            visit(arg.get());
                            llvm::Value* val = lastValue;
                            
                            // Cast if necessary
                            if (argIdx < (int)ft->getNumParams()) {
                                val = castValue(val, ft->getParamType(argIdx));
                            }
                            
                            args.push_back(val);
                            argIdx++;
                        }
                        lastValue = builder->CreateCall(ft, func.getCallee(), args);
                        return;
                    }
                    current = current->baseClass;
                }
            }
        }

        if (dynamic_cast<ast::SuperExpression*>(prop->expression.get())) {
            // super.method()
            if (!currentClass || currentClass->kind != TypeKind::Class) return;
            auto classType = std::static_pointer_cast<ClassType>(currentClass);
            if (!classType->baseClass) return;
            
            std::string baseClassName = classType->baseClass->name;
            std::string methodName = prop->name;
            
            // Static dispatch to base class method
            std::string funcName = baseClassName + "_" + methodName;
            llvm::Function* func = module->getFunction(funcName);
            if (func) {
                std::vector<llvm::Value*> args;
                llvm::Function* currentFunc = builder->GetInsertBlock()->getParent();
                args.push_back(currentFunc->getArg(0)); // this
                
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    args.push_back(lastValue);
                }
                lastValue = builder->CreateCall(func, args);
            }
            return;
        }

        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
            std::string className = classType->name;
            std::string methodName = prop->name;
            
            // 1. Evaluate Object
            visit(prop->expression.get());
            llvm::Value* objPtr = lastValue;
            
            llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
            if (!classStruct) return;
            
            const auto& layout = classLayouts[className];
            if (!layout.methodIndices.count(methodName)) return;
            int methodIndex = layout.methodIndices.at(methodName);

            llvm::Value* typedObjPtr = builder->CreateBitCast(objPtr, llvm::PointerType::getUnqual(classStruct));
            llvm::Value* vptrPtr = builder->CreateStructGEP(classStruct, typedObjPtr, 0);
            
            llvm::StructType* vtableStruct = llvm::StructType::getTypeByName(*context, className + "_VTable");
            llvm::Value* vptr = builder->CreateLoad(llvm::PointerType::getUnqual(vtableStruct), vptrPtr);
            
            // 4. Load Function Pointer (index + 1 because of parentVTable at index 0)
            llvm::Value* funcPtrPtr = builder->CreateStructGEP(vtableStruct, vptr, methodIndex + 1);
            
            auto methodType = layout.allMethods[methodIndex].second;
            std::vector<llvm::Type*> paramTypes;
            paramTypes.push_back(llvm::PointerType::getUnqual(*context)); // this
            for (const auto& param : methodType->paramTypes) {
                paramTypes.push_back(getLLVMType(param));
            }
            llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(methodType->returnType), paramTypes, false);
            
            llvm::Value* funcPtr = builder->CreateLoad(llvm::PointerType::getUnqual(ft), funcPtrPtr);
            
            // 5. Call
            std::vector<llvm::Value*> args;
            args.push_back(typedObjPtr); // this
            
            int argIdx = 0;
            for (auto& arg : node->arguments) {
                visit(arg.get());
                llvm::Value* val = lastValue;
                
                // Cast if necessary
                if (argIdx + 1 < (int)ft->getNumParams()) {
                    val = castValue(val, ft->getParamType(argIdx + 1));
                }
                
                args.push_back(val);
                argIdx++;
            }
            
            lastValue = builder->CreateCall(ft, funcPtr, args);
            return;
        }

        if (prop->name == "log") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "console") {
                    if (node->arguments.empty()) return;
                    
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    if (!arg) {
                        llvm::errs() << "Error: console.log argument evaluated to null\n";
                        return;
                    }

                    llvm::Type* argType = arg->getType();

                    if (argType->isVoidTy()) {
                        llvm::errs() << "Error: Argument to console.log is void\n";
                        return;
                    }

                    std::string funcName = "ts_console_log";
                    llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

                    if (argType->isIntegerTy(64)) {
                        funcName = "ts_console_log_int";
                        paramType = llvm::Type::getInt64Ty(*context);
                    } else if (argType->isDoubleTy()) {
                        funcName = "ts_console_log_double";
                        paramType = llvm::Type::getDoubleTy(*context);
                    } else if (argType->isIntegerTy(1)) {
                        funcName = "ts_console_log_bool";
                        paramType = llvm::Type::getInt1Ty(*context);
                    }

                    llvm::FunctionType* ft = llvm::FunctionType::get(
                        llvm::Type::getVoidTy(*context), { paramType }, false);
                    llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

                    builder->CreateCall(logFn, { arg });
                    lastValue = nullptr;
                    return;
                }
            }
        } else if (prop->name == "readFileSync") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "fs") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee readFn = module->getOrInsertFunction("ts_fs_readFileSync",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), 
                            { llvm::PointerType::getUnqual(*context) }, false));
                    
                    lastValue = builder->CreateCall(readFn, { arg });
                    return;
                }
            }
        } else if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
            if (obj->name == "Math") {
                if (prop->name == "min" || prop->name == "max") {
                    if (node->arguments.size() < 2) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg1 = lastValue;
                    visit(node->arguments[1].get());
                    llvm::Value* arg2 = lastValue;
                    
                    std::string funcName = (prop->name == "min") ? "ts_math_min" : "ts_math_max";
                    llvm::FunctionCallee fn = module->getOrInsertFunction(funcName,
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg1, arg2 });
                    return;
                } else if (prop->name == "abs") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_abs",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getInt64Ty(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                } else if (prop->name == "floor") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    if (arg->getType()->isIntegerTy(64)) {
                        // floor(int) is just int
                        lastValue = arg;
                        return;
                    }
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_math_floor",
                        llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                            { llvm::Type::getDoubleTy(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                }
            }
        }
        
        if (prop->name == "charCodeAt") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* index = lastValue;
             
             if (index->getType()->isDoubleTy()) {
                 index = builder->CreateFPToSI(index, llvm::Type::getInt64Ty(*context));
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_charCodeAt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
             
             lastValue = builder->CreateCall(fn, { obj, index });
             return;
        } else if (prop->name == "split") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* sep = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_split",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, sep });
             return;
        } else if (prop->name == "trim") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_trim",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj });
             return;
        } else if (prop->name == "startsWith") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* prefix = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_startsWith",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, prefix });
             return;
        } else if (prop->name == "substring") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             if (node->arguments.empty()) return;
             
             visit(node->arguments[0].get());
             llvm::Value* start = lastValue;
             if (start->getType()->isDoubleTy()) {
                 start = builder->CreateFPToSI(start, llvm::Type::getInt64Ty(*context));
             }

             llvm::Value* end = nullptr;
             if (node->arguments.size() > 1) {
                 visit(node->arguments[1].get());
                 end = lastValue;
                 if (end->getType()->isDoubleTy()) {
                     end = builder->CreateFPToSI(end, llvm::Type::getInt64Ty(*context));
                 }
             } else {
                 // Default end to length
                 llvm::FunctionCallee lenFn = module->getOrInsertFunction("ts_string_length",
                     llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                         { llvm::PointerType::getUnqual(*context) }, false));
                 end = builder->CreateCall(lenFn, { obj });
             }

             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_string_substring",
                 llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context), llvm::Type::getInt64Ty(*context) }, false));
             lastValue = builder->CreateCall(fn, { obj, start, end });
             return;
        } else if (prop->name == "sort") {
             visit(prop->expression.get());
             llvm::Value* obj = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_array_sort",
                 llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             builder->CreateCall(fn, { obj });
             lastValue = nullptr;
             return;
        } else if (prop->name == "set") {
             visit(prop->expression.get());
             llvm::Value* map = lastValue;
             
             if (node->arguments.size() < 2) return;
             visit(node->arguments[0].get());
             llvm::Value* key = lastValue;
             visit(node->arguments[1].get());
             llvm::Value* value = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_set",
                 llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));
             builder->CreateCall(fn, { map, key, value });
             lastValue = nullptr;
             return;
        } else if (prop->name == "get") {
             visit(prop->expression.get());
             llvm::Value* map = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* key = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_get",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             lastValue = builder->CreateCall(fn, { map, key });
             return;
        } else if (prop->name == "has") {
             visit(prop->expression.get());
             llvm::Value* map = lastValue;
             
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* key = lastValue;
             
             llvm::FunctionCallee fn = module->getOrInsertFunction("ts_map_has",
                 llvm::FunctionType::get(llvm::Type::getInt1Ty(*context),
                     { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) }, false));
             llvm::Value* res = builder->CreateCall(fn, { map, key });
             lastValue = builder->CreateICmpNE(res, llvm::ConstantInt::get(res->getType(), 0), "tobool");
             return;
        } else if (prop->name == "md5") {
            if (auto obj = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                if (obj->name == "crypto") {
                    if (node->arguments.empty()) return;
                    visit(node->arguments[0].get());
                    llvm::Value* arg = lastValue;
                    
                    llvm::FunctionCallee fn = module->getOrInsertFunction("ts_crypto_md5",
                        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context),
                            { llvm::PointerType::getUnqual(*context) }, false));
                    lastValue = builder->CreateCall(fn, { arg });
                    return;
                }
            }
        }
    }

    // User function call
    if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
        if (namedValues.count(id->name)) {
            llvm::Value* val = namedValues[id->name];
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(val)) {
                llvm::Value* funcPtr = builder->CreateLoad(alloca->getAllocatedType(), alloca, id->name.c_str());
                
                std::vector<llvm::Value*> args;
                std::vector<llvm::Type*> argTypes;
                for (auto& arg : node->arguments) {
                    visit(arg.get());
                    llvm::Value* v = lastValue;
                    // Hack: Convert int to double for arrow functions
                    if (v->getType()->isIntegerTy(64)) {
                        v = builder->CreateSIToFP(v, llvm::Type::getDoubleTy(*context));
                    }
                    args.push_back(v);
                    argTypes.push_back(v->getType());
                }
                
                llvm::Type* retType = llvm::Type::getDoubleTy(*context);
                llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
                lastValue = builder->CreateCall(ft, funcPtr, args);
                return;
            }
        }

        if (id->name == "parseInt") {
             if (node->arguments.empty()) return;
             visit(node->arguments[0].get());
             llvm::Value* arg = lastValue;

             llvm::FunctionCallee parseFn = module->getOrInsertFunction("ts_parseInt",
                 llvm::FunctionType::get(llvm::Type::getInt64Ty(*context),
                     { llvm::PointerType::getUnqual(*context) }, false));
             
             lastValue = builder->CreateCall(parseFn, { arg });
             return;
        }

        if (id->name == "ts_console_log") {
            if (node->arguments.empty()) return;
            visit(node->arguments[0].get());
            llvm::Value* arg = lastValue;
            
            std::string funcName = "ts_console_log";
            llvm::Type* paramType = llvm::PointerType::getUnqual(*context);

            if (arg->getType()->isIntegerTy(64)) {
                funcName = "ts_console_log_int";
                paramType = llvm::Type::getInt64Ty(*context);
            } else if (arg->getType()->isDoubleTy()) {
                funcName = "ts_console_log_double";
                paramType = llvm::Type::getDoubleTy(*context);
            } else if (arg->getType()->isIntegerTy(1)) {
                funcName = "ts_console_log_bool";
                paramType = llvm::Type::getInt1Ty(*context);
            }

            llvm::FunctionType* ft = llvm::FunctionType::get(
                llvm::Type::getVoidTy(*context), { paramType }, false);
            llvm::FunctionCallee logFn = module->getOrInsertFunction(funcName, ft);

            builder->CreateCall(logFn, { arg });
            lastValue = nullptr;
            return;
        }

        std::string funcName = id->name;
        
        std::vector<llvm::Value*> args;
        std::vector<std::shared_ptr<Type>> argTypes;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            if (lastValue) {
                args.push_back(lastValue);
                if (arg->inferredType) {
                    argTypes.push_back(arg->inferredType);
                } else {
                    // Fallback if type inference failed
                    argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
            }
        }

        std::string mangledName = Monomorphizer::generateMangledName(funcName, argTypes);

        llvm::Function* func = module->getFunction(mangledName);
        if (func) {
            lastValue = builder->CreateCall(func, args);
        } else {
            llvm::errs() << "Function not found: " << mangledName << "\n";
            // TODO: Error handling
            lastValue = nullptr;
        }
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
        llvm::Value* variable = namedValues[id->name];
        if (!variable) {
            llvm::errs() << "Error: Unknown variable name " << id->name << "\n";
            return;
        }

        // 4. Store the value
        llvm::Type* varType = llvm::dyn_cast<llvm::AllocaInst>(variable)->getAllocatedType();
        val = castValue(val, varType);
        builder->CreateStore(val, variable);
    } else if (auto elem = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
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

void IRGenerator::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", 
        llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), {}, false));
    
    llvm::Value* arr = builder->CreateCall(createFn);

    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
        llvm::FunctionType::get(llvm::Type::getVoidTy(*context), 
            { llvm::PointerType::getUnqual(*context), llvm::Type::getInt64Ty(*context) }, false));

    for (auto& el : node->elements) {
        visit(el.get());
        llvm::Value* val = lastValue;
        if (!val) continue;

        if (val->getType()->isDoubleTy()) {
             val = builder->CreateBitCast(val, llvm::Type::getInt64Ty(*context));
        } else if (val->getType()->isPointerTy()) {
             val = builder->CreatePtrToInt(val, llvm::Type::getInt64Ty(*context));
        }
        
        builder->CreateCall(pushFn, { arr, val });
    }
    
    lastValue = arr;
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
                    llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), paramTypes, false);
                    
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
        
        llvm::errs() << "Error: Unknown property " << node->name << "\n";
        lastValue = nullptr;
    }
}

void IRGenerator::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    if (node->op == "typeof") {
        visit(node->operand.get());
        llvm::Value* val = lastValue;
        
        if (val->getType()->isDoubleTy()) {
            llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", 
                builder->getPtrTy(), builder->getPtrTy());
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("number");
            lastValue = builder->CreateCall(createFn, {strPtr});
            return;
        }
        
        if (val->getType()->isIntegerTy(1)) {
            llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", 
                builder->getPtrTy(), builder->getPtrTy());
            llvm::Value* strPtr = builder->CreateGlobalStringPtr("boolean");
            lastValue = builder->CreateCall(createFn, {strPtr});
            return;
        }

        if (val->getType()->isPointerTy()) {
            llvm::FunctionCallee typeofFn = module->getOrInsertFunction("ts_typeof",
                builder->getPtrTy(), builder->getPtrTy());
            lastValue = builder->CreateCall(typeofFn, {val});
            return;
        }

        llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_string_create", 
            builder->getPtrTy(), builder->getPtrTy());
        llvm::Value* strPtr = builder->CreateGlobalStringPtr("unknown");
        lastValue = builder->CreateCall(createFn, {strPtr});
        return;
    }

    visit(node->operand.get());
    llvm::Value* operandV = lastValue;

    if (node->op == "-") {
        if (operandV->getType()->isDoubleTy()) {
            lastValue = builder->CreateFNeg(operandV, "negtmp");
        } else if (operandV->getType()->isIntegerTy()) {
            lastValue = builder->CreateNeg(operandV, "negtmp");
        }
    } else if (node->op == "!") {
        if (operandV->getType()->isDoubleTy()) {
            operandV = builder->CreateFCmpONE(operandV, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "tobool");
        } else if (operandV->getType()->isIntegerTy() && operandV->getType()->getIntegerBitWidth() > 1) {
             operandV = builder->CreateICmpNE(operandV, llvm::ConstantInt::get(operandV->getType(), 0), "tobool");
        }
        lastValue = builder->CreateNot(operandV, "nottmp");
    }
}

void IRGenerator::visitTemplateExpression(ast::TemplateExpression* node) {
    llvm::Function* createFn = module->getFunction("ts_string_create");
    if (!createFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(
            llvm::PointerType::getUnqual(*context),
            args, false);
        createFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_create", module.get());
    }
    
    llvm::Function* concatFn = module->getFunction("ts_string_concat");
    if (!concatFn) {
        std::vector<llvm::Type*> args = { llvm::PointerType::getUnqual(*context), llvm::PointerType::getUnqual(*context) };
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), args, false);
        concatFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_concat", module.get());
    }

    llvm::Constant* headStr = builder->CreateGlobalStringPtr(node->head);
    llvm::Value* currentStr = builder->CreateCall(createFn, { headStr });
    
    for (auto& span : node->spans) {
        visit(span.expression.get());
        llvm::Value* exprVal = lastValue;
        
        if (exprVal->getType()->isIntegerTy()) {
            llvm::Function* fromIntFn = module->getFunction("ts_string_from_int");
            if (!fromIntFn) {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getInt64Ty(*context) }, false);
                fromIntFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_from_int", module.get());
            }
            exprVal = builder->CreateCall(fromIntFn, { exprVal });
        } else if (exprVal->getType()->isDoubleTy()) {
            llvm::Function* fromDoubleFn = module->getFunction("ts_string_from_double");
            if (!fromDoubleFn) {
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::PointerType::getUnqual(*context), { llvm::Type::getDoubleTy(*context) }, false);
                fromDoubleFn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, "ts_string_from_double", module.get());
            }
            exprVal = builder->CreateCall(fromDoubleFn, { exprVal });
        }
        
        currentStr = builder->CreateCall(concatFn, { currentStr, exprVal });
        
        llvm::Constant* litStr = builder->CreateGlobalStringPtr(span.literal);
        llvm::Value* litVal = builder->CreateCall(createFn, { litStr });
        currentStr = builder->CreateCall(concatFn, { currentStr, litVal });
    }
    
    lastValue = currentStr;
}

void IRGenerator::visitAsExpression(ast::AsExpression* node) {
    visit(node->expression.get());
    lastValue = castValue(lastValue, getLLVMType(node->inferredType));
}

} // namespace ts
