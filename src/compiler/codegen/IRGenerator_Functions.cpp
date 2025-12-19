#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

void IRGenerator::generatePrototypes(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        std::vector<llvm::Type*> argTypes;
        for (const auto& argType : spec.argTypes) {
            argTypes.push_back(getLLVMType(argType));
        }

        llvm::Type* returnType = getLLVMType(spec.returnType);
        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, argTypes, false);

        llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, spec.specializedName, module.get());
    }
}

void IRGenerator::generateBodies(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        llvm::Function* function = module->getFunction(spec.specializedName);
        if (!function) continue;

        if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            if (methodNode->isAbstract || !methodNode->hasBody) continue;
        }

        if (!function->empty()) continue;

        bool isAsync = false;
        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            isAsync = funcNode->isAsync;
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            isAsync = methodNode->isAsync;
        }

        if (isAsync) {
            generateAsyncFunctionBody(function, spec.node, spec.argTypes, spec.classType, spec.specializedName);
            continue;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        namedValues.clear();
        currentClass = spec.classType;
        typeEnvironment.clear();
        currentAsyncFrame = nullptr;
        currentAsyncContext = nullptr;

        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            // Populate type environment
            for (size_t i = 0; i < funcNode->typeParameters.size(); ++i) {
                if (i < spec.typeArguments.size()) {
                    typeEnvironment[funcNode->typeParameters[i]->name] = spec.typeArguments[i];
                }
            }

            unsigned idx = 0;
            auto argIt = function->arg_begin();

            for (size_t pIdx = 0; pIdx < funcNode->parameters.size(); ++pIdx) {
                auto param = funcNode->parameters[pIdx].get();

                if (param->isRest) {
                    // Create an empty array
                    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create",
                        llvm::FunctionType::get(builder->getPtrTy(), {}, false));
                    llvm::Value* arr = builder->CreateCall(createFn);

                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                            { builder->getPtrTy(), builder->getPtrTy() }, false));

                    // Collect all remaining arguments into the array
                    while (argIt != function->arg_end()) {
                        llvm::Value* argVal = &*argIt;
                        // Box the value if it's a primitive
                        std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                        llvm::Value* boxedVal = boxValue(argVal, argType);
                        builder->CreateCall(pushFn, { arr, boxedVal });
                        ++argIt;
                        ++idx;
                    }

                    generateDestructuring(arr, nullptr, param->name.get());
                    break; // Rest parameter must be the last one
                } else {
                    if (argIt != function->arg_end()) {
                        auto argVal = &*argIt;
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            argVal->setName(id->name);
                        }
                        generateDestructuring(argVal, spec.argTypes[idx], param->name.get());
                        ++argIt;
                        ++idx;
                    } else {
                        // Handle missing parameters (optional/default)
                        llvm::Value* initVal = nullptr;
                        if (param->initializer) {
                            visit(param->initializer.get());
                            initVal = lastValue;
                        } else {
                            // Default to undefined (null pointer for now)
                            initVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        }
                        generateDestructuring(initVal, nullptr, param->name.get());
                    }
                }
            }

            for (auto& stmt : funcNode->body) {
                visit(stmt.get());
            }
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            auto argIt = function->arg_begin();
            if (!methodNode->isStatic) {
                // Handle 'this' parameter (first argument)
                if (argIt != function->arg_end()) {
                    argIt->setName("this");
                    llvm::AllocaInst* alloca = createEntryBlockAlloca(function, "this", argIt->getType());
                    builder->CreateStore(&*argIt, alloca);
                    namedValues["this"] = alloca;
                    ++argIt;
                }
            }

            // Handle explicit parameters
            unsigned idx = 0;
            for (size_t pIdx = 0; pIdx < methodNode->parameters.size(); ++pIdx) {
                auto param = methodNode->parameters[pIdx].get();

                if (param->isRest) {
                    // Create an empty array
                    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create",
                        llvm::FunctionType::get(builder->getPtrTy(), {}, false));
                    llvm::Value* arr = builder->CreateCall(createFn);

                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push",
                        llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                            { builder->getPtrTy(), builder->getPtrTy() }, false));

                    // Collect all remaining arguments into the array
                    while (argIt != function->arg_end()) {
                        llvm::Value* argVal = &*argIt;
                        unsigned argTypeIdx = methodNode->isStatic ? idx : idx + 1;
                        std::shared_ptr<Type> argType = (argTypeIdx < spec.argTypes.size()) ? spec.argTypes[argTypeIdx] : nullptr;
                        llvm::Value* boxedVal = boxValue(argVal, argType);
                        builder->CreateCall(pushFn, { arr, boxedVal });
                        ++argIt;
                        ++idx;
                    }

                    generateDestructuring(arr, nullptr, param->name.get());
                    break;
                } else {
                    if (argIt != function->arg_end()) {
                        auto argVal = &*argIt;
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            argVal->setName(id->name);
                        }
                        unsigned argTypeIdx = methodNode->isStatic ? idx : idx + 1;
                        generateDestructuring(argVal, (argTypeIdx < spec.argTypes.size()) ? spec.argTypes[argTypeIdx] : nullptr, param->name.get());
                        ++argIt;
                        ++idx;
                    } else {
                        // Handle missing parameters (optional/default)
                        llvm::Value* initVal = nullptr;
                        if (param->initializer) {
                            visit(param->initializer.get());
                            initVal = lastValue;
                        } else {
                            initVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        }
                        generateDestructuring(initVal, nullptr, param->name.get());
                    }
                }
            }

            for (auto& stmt : methodNode->body) {
                visit(stmt.get());
            }
        }

        if (!builder->GetInsertBlock()->getTerminator()) {
            llvm::Type* retType = function->getReturnType();
            if (retType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                builder->CreateRet(llvm::Constant::getNullValue(retType));
            }
        }
    }
}

void IRGenerator::visitArrowFunction(ast::ArrowFunction* node) {
    static int lambdaCounter = 0;
    std::string name = "lambda_" + std::to_string(lambdaCounter++);
    
    std::vector<llvm::Type*> argTypes;
    for (auto& param : node->parameters) {
        argTypes.push_back(builder->getPtrTy()); // TsValue*
    }
    
    llvm::Type* retType = builder->getPtrTy(); // TsValue*
    
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name, module.get());
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();
    auto oldNamedValues = namedValues;

    if (node->isAsync) {
        std::vector<std::shared_ptr<Type>> argTypes;
        auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
        for (size_t i = 0; i < node->parameters.size(); ++i) {
            if (funcType && i < funcType->paramTypes.size()) {
                argTypes.push_back(funcType->paramTypes[i]);
            } else {
                argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
        }
        generateAsyncFunctionBody(function, node, argTypes, nullptr, name);
        builder->SetInsertPoint(oldBB);
        namedValues = oldNamedValues;
        lastValue = boxValue(function, node->inferredType);
        return;
    }
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);
    
    namedValues.clear();
    
    unsigned idx = 0;
    auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
    for (auto& arg : function->args()) {
        auto param = node->parameters[idx].get();
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
        }
        std::shared_ptr<Type> paramType = (funcType && idx < funcType->paramTypes.size()) ? funcType->paramTypes[idx] : std::make_shared<Type>(TypeKind::Any);
        
        // Unbox the argument
        llvm::Value* unboxedArg = unboxValue(&arg, paramType);
        generateDestructuring(unboxedArg, paramType, param->name.get());
        idx++;
    }
    
    visit(node->body.get());
    
    if (lastValue && !builder->GetInsertBlock()->getTerminator()) {
        // Box the return value
        std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
        llvm::Value* boxedRet = boxValue(lastValue, returnType);
        builder->CreateRet(boxedRet);
    } else if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }
    
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = boxValue(function, node->inferredType);
}

void IRGenerator::visitFunctionExpression(ast::FunctionExpression* node) {
    static int funcExprCounter = 0;
    std::string name = node->name.empty() ? "func_expr_" + std::to_string(funcExprCounter++) : node->name;
    
    std::vector<llvm::Type*> argTypes;
    for (auto& param : node->parameters) {
        argTypes.push_back(builder->getPtrTy()); // TsValue*
    }
    
    llvm::Type* retType = builder->getPtrTy(); // TsValue*
    
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name, module.get());
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();
    auto oldNamedValues = namedValues;

    if (node->isAsync) {
        std::vector<std::shared_ptr<Type>> argTypes;
        auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
        for (size_t i = 0; i < node->parameters.size(); ++i) {
            if (funcType && i < funcType->paramTypes.size()) {
                argTypes.push_back(funcType->paramTypes[i]);
            } else {
                argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
        }
        generateAsyncFunctionBody(function, node, argTypes, nullptr, name);
        builder->SetInsertPoint(oldBB);
        namedValues = oldNamedValues;
        lastValue = boxValue(function, node->inferredType);
        return;
    }
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);
    
    namedValues.clear();
    
    unsigned idx = 0;
    auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
    for (auto& arg : function->args()) {
        auto param = node->parameters[idx].get();
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
        }
        std::shared_ptr<Type> paramType = (funcType && idx < funcType->paramTypes.size()) ? funcType->paramTypes[idx] : std::make_shared<Type>(TypeKind::Any);
        
        // Unbox the argument
        llvm::Value* unboxedArg = unboxValue(&arg, paramType);
        generateDestructuring(unboxedArg, paramType, param->name.get());
        idx++;
    }
    
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (lastValue) {
            std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
            llvm::Value* boxedRet = boxValue(lastValue, returnType);
            builder->CreateRet(boxedRet);
        } else {
            builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
        }
    }
    
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = boxValue(function, node->inferredType);
}

void IRGenerator::generateAsyncFunctionBody(llvm::Function* entryFunc, ast::Node* node, const std::vector<std::shared_ptr<Type>>& argTypes, std::shared_ptr<Type> classType, const std::string& specializedName) {
    // Implementation for generating the body of async functions
    // This is a placeholder, actual implementation will vary
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", entryFunc);
    builder->SetInsertPoint(bb);

    // Example: Create a dummy return for async functions
    builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
}

} // namespace ts


