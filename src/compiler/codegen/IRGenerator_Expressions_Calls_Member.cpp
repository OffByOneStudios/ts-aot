#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

bool IRGenerator::tryGenerateMemberCall(ast::CallExpression* node) {
    auto prop = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get());
    if (!prop) return false;

    if (tryGenerateBuiltinCall(node, prop)) {
        return true;
    }

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Namespace) {
        // Namespace call: math.add(1, 2)
        std::vector<std::shared_ptr<Type>> argTypes;
        for (auto& arg : node->arguments) {
            argTypes.push_back(arg->inferredType);
        }
        std::string specializedName = Monomorphizer::generateMangledName(prop->name, argTypes, node->resolvedTypeArguments);
        
        // Find the specialization to get the return type
        std::shared_ptr<Type> returnType = std::make_shared<Type>(TypeKind::Any);
        for (const auto& spec : specializations) {
            if (spec.specializedName == specializedName) {
                returnType = spec.returnType;
                break;
            }
        }

        std::vector<llvm::Type*> paramTypes;
        for (const auto& argType : argTypes) {
            paramTypes.push_back(getLLVMType(argType));
        }
        
        llvm::FunctionType* ft = llvm::FunctionType::get(getLLVMType(returnType), paramTypes, false);
        llvm::FunctionCallee func = module->getOrInsertFunction(specializedName, ft);
        
        std::vector<llvm::Value*> args;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            args.push_back(lastValue);
        }
        
        lastValue = builder->CreateCall(ft, func.getCallee(), args);
        return true;
    }

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(prop->expression->inferredType);
        if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            
            // Static method call
            auto current = cls;
            while (current) {
                if (current->staticMethods.count(prop->name)) {
                    if (current->name == "Promise" && prop->name == "resolve") {
                        llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                            builder->getPtrTy(), builder->getPtrTy());
                        
                        llvm::Value* val;
                        std::shared_ptr<Type> valType;
                        if (node->arguments.empty()) {
                            val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                            valType = std::make_shared<Type>(TypeKind::Void);
                        } else {
                            visit(node->arguments[0].get());
                            val = lastValue;
                            valType = node->arguments[0]->inferredType;
                        }
                        
                        llvm::Value* boxedVal = boxValue(val, valType);
                        lastValue = builder->CreateCall(resolveFn, { boxedVal });
                        return true;
                    }

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
                    return true;
                }
                current = current->baseClass;
            }
        }
    }

    if (dynamic_cast<ast::SuperExpression*>(prop->expression.get())) {
        // super.method()
        if (!currentClass || currentClass->kind != TypeKind::Class) return false;
        auto classType = std::static_pointer_cast<ClassType>(currentClass);
        if (!classType->baseClass) return false;
        
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
        return true;
    }

    if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Class) {
        auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
        std::string className = classType->name;
        std::string methodName = prop->name;
        
        if (className == "Date") {
            visit(prop->expression.get());
            llvm::Value* dateObj = lastValue;
            
            std::string funcName = "Date_" + methodName;
            llvm::Function* func = module->getFunction(funcName);
            if (!func) {
                std::vector<llvm::Type*> argTypes = { llvm::PointerType::getUnqual(*context) };
                llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getInt64Ty(*context), argTypes, false);
                func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, funcName, module.get());
            }
            
            std::vector<llvm::Value*> args = { dateObj };
            for (auto& arg : node->arguments) {
                visit(arg.get());
                args.push_back(lastValue);
            }
            lastValue = builder->CreateCall(func, args);
            return true;
        } else if (className == "RegExp") {
            // ... existing RegExp code ...
            return true;
        } else if (className == "Promise") {
            visit(prop->expression.get());
            llvm::Value* promiseObj = lastValue;
            
            if (methodName == "then") {
                llvm::FunctionCallee thenFn = module->getOrInsertFunction("ts_promise_then",
                    builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy());
                
                visit(node->arguments[0].get());
                llvm::Value* callback = lastValue;
                
                // Box the callback if it's not already a TsValue*
                llvm::Value* boxedCallback = boxValue(callback, node->arguments[0]->inferredType);
                
                lastValue = builder->CreateCall(thenFn, { promiseObj, boxedCallback });
                return true;
            } else if (methodName == "resolve") {
                llvm::FunctionCallee resolveFn = module->getOrInsertFunction("ts_promise_resolve",
                    builder->getPtrTy(), builder->getPtrTy());
                
                llvm::Value* val;
                std::shared_ptr<Type> valType;
                if (node->arguments.empty()) {
                    val = llvm::ConstantPointerNull::get(builder->getPtrTy());
                    valType = std::make_shared<Type>(TypeKind::Void);
                } else {
                    visit(node->arguments[0].get());
                    val = lastValue;
                    valType = node->arguments[0]->inferredType;
                }
                
                llvm::Value* boxedVal = boxValue(val, valType);
                lastValue = builder->CreateCall(resolveFn, { boxedVal });
                return true;
            }
        }

        // 1. Evaluate Object
        visit(prop->expression.get());
        llvm::Value* objPtr = lastValue;
        
        llvm::StructType* classStruct = llvm::StructType::getTypeByName(*context, className);
        if (!classStruct) {
            return false;
        }
        
        if (!classLayouts.count(className)) {
            return false;
        }
        const auto& layout = classLayouts[className];
        if (!layout.methodIndices.count(methodName)) {
            return false;
        }
        
        int methodIndex = layout.methodIndices.at(methodName);

        // 2. Get VTable Pointer
        llvm::Value* vptrPtr = builder->CreateStructGEP(classStruct, objPtr, 0);
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
        args.push_back(objPtr); // this
        
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
        return true;
    }

    return false;
}

} // namespace ts
