#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"

namespace ts {
using namespace ast;

void IRGenerator::generatePrototypes(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        if (module->getFunction(spec.specializedName)) continue;

        std::vector<llvm::Type*> argTypes;
        
        // Always add context argument first
        argTypes.push_back(builder->getPtrTy());

        for (const auto& argType : spec.argTypes) {
            argTypes.push_back(getLLVMType(argType));
        }

        llvm::Type* returnType = getLLVMType(spec.returnType);
        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, argTypes, false);

        llvm::errs() << "Creating prototype: " << spec.specializedName << " with " << argTypes.size() << " args\n";
        llvm::Function* func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, spec.specializedName, module.get());
        
        // Inline specialized functions for performance
        if (spec.specializedName.find("Vector3") != std::string::npos || 
            spec.specializedName.find("Ray") != std::string::npos ||
            spec.specializedName.find("Sphere") != std::string::npos) {
            func->addFnAttr(llvm::Attribute::AlwaysInline);
        }
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

        llvm::errs() << "Generating body for: " << spec.specializedName << " isAsync: " << isAsync << "\n";

        if (isAsync) {
            generateAsyncFunctionBody(function, spec.node, spec.argTypes, spec.classType, spec.specializedName);
            continue;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        namedValues.clear();
        lastValue = nullptr;
        anonVarCounter = 0;

        currentClass = spec.classType;
        currentReturnType = spec.returnType;
        typeEnvironment.clear();
        currentAsyncFrame = nullptr;
        currentAsyncContext = nullptr;
        currentContext = nullptr;

        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            if (funcNode->isAsync) {
                generateAsyncFunctionBody(function, funcNode, spec.argTypes, nullptr, spec.specializedName);
                continue;
            }
            // Populate type environment
            for (size_t i = 0; i < funcNode->typeParameters.size(); ++i) {
                if (i < spec.typeArguments.size()) {
                    typeEnvironment[funcNode->typeParameters[i]->name] = spec.typeArguments[i];
                }
            }

            auto argIt = function->arg_begin();
            // Skip context argument
            if (argIt != function->arg_end()) {
                argIt->setName("context");
                currentContext = &*argIt;
                ++argIt;
            }

            unsigned idx = 0; // Index into spec.argTypes

            for (size_t pIdx = 0; pIdx < funcNode->parameters.size(); ++pIdx) {
                auto param = funcNode->parameters[pIdx].get();

                if (param->isRest) {
                    // Create an empty array
                    llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", createFt);
                    llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

                    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                            { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);

                    // Collect all remaining arguments into the array
                    while (argIt != function->arg_end()) {
                        llvm::Value* argVal = &*argIt;
                        // Box the value if it's a primitive
                        std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                        llvm::Value* boxedVal = boxValue(argVal, argType);
                        createCall(pushFt, pushFn.getCallee(), { arr, boxedVal });
                        ++argIt;
                        ++idx;
                    }

                    generateDestructuring(arr, nullptr, param->name.get());
                    break; // Rest parameter must be the last one
                } else {
                    if (argIt != function->arg_end()) {
                        auto argVal = &*argIt;
                        std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            argVal->setName(id->name);
                        }
                        generateDestructuring(argVal, argType, param->name.get());
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
            if (methodNode->isAsync) {
                generateAsyncFunctionBody(function, methodNode, spec.argTypes, spec.classType, spec.specializedName);
                continue;
            }
            auto argIt = function->arg_begin();
            
            // Skip context argument
            if (argIt != function->arg_end()) {
                argIt->setName("context");
                currentContext = &*argIt;
                ++argIt;
            }

            unsigned idx = 0; // Index into spec.argTypes

            if (!methodNode->isStatic) {
                // Handle 'this' parameter
                if (argIt != function->arg_end()) {
                    argIt->setName("this");
                    llvm::AllocaInst* alloca = createEntryBlockAlloca(function, "this", argIt->getType());
                    builder->CreateStore(&*argIt, alloca);
                    namedValues["this"] = alloca;
                    ++argIt;
                    idx++;
                }
            }

            // Handle explicit parameters
            for (size_t pIdx = 0; pIdx < methodNode->parameters.size(); ++pIdx) {
                auto param = methodNode->parameters[pIdx].get();

                if (param->isRest) {
                    // Create an empty array
                    llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee createFn = module->getOrInsertFunction("ts_array_create", createFt);
                    llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

                    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                            { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);

                    // Collect all remaining arguments into the array
                    while (argIt != function->arg_end()) {
                        llvm::Value* argVal = &*argIt;
                        std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                        llvm::Value* boxedVal = boxValue(argVal, argType);
                        createCall(pushFt, pushFn.getCallee(), { arr, boxedVal });
                        ++argIt;
                        ++idx;
                    }

                    generateDestructuring(arr, nullptr, param->name.get());
                    break;
                } else {
                    if (argIt != function->arg_end()) {
                        auto argVal = &*argIt;
                        std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            argVal->setName(id->name);
                        }
                        generateDestructuring(argVal, argType, param->name.get());
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
    argTypes.push_back(builder->getPtrTy()); // context first
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
    lastValue = nullptr;
    anonVarCounter = 0;
    
    // Force return type to Any for arrow functions to ensure compatibility with runtime handlers
    auto oldReturnType = currentReturnType;
    currentReturnType = std::make_shared<Type>(TypeKind::Any);
    
    auto argIt = function->arg_begin();
    if (argIt != function->arg_end()) {
        argIt->setName("context");
        currentContext = &*argIt;
        ++argIt;
    }

    unsigned idx = 0;
    auto funcType = std::static_pointer_cast<FunctionType>(node->inferredType);
    while (argIt != function->arg_end() && idx < node->parameters.size()) {
        auto param = node->parameters[idx].get();
        auto& arg = *argIt;
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
        }
        std::shared_ptr<Type> paramType = (funcType && idx < funcType->paramTypes.size()) ? funcType->paramTypes[idx] : std::make_shared<Type>(TypeKind::Any);
        
        // Unbox the argument
        llvm::Value* unboxedArg = unboxValue(&arg, paramType);
        generateDestructuring(unboxedArg, paramType, param->name.get());
        
        ++argIt;
        ++idx;
    }
    
    if (node->body) {
        llvm::errs() << "Visiting arrow function body: " << node->body->getKind() << " at " << node->body.get() << "\n";
        visit(node->body.get());
    } else {
        llvm::errs() << "Arrow function body is NULL! Node at " << node << "\n";
    }
    
    if (node->body && node->body->getKind() != "BlockStatement" && lastValue && !builder->GetInsertBlock()->getTerminator()) {
        // Box the return value
        std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
        llvm::Value* boxedRet = boxValue(lastValue, returnType);
        builder->CreateRet(boxedRet);
    } else if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }
    
    currentReturnType = oldReturnType;
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = function;
}

void IRGenerator::visitFunctionExpression(ast::FunctionExpression* node) {
    static int funcExprCounter = 0;
    std::string name = node->name.empty() ? "func_expr_" + std::to_string(funcExprCounter++) : node->name;
    
    std::vector<llvm::Type*> argTypes;
    argTypes.push_back(builder->getPtrTy()); // context first
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
    lastValue = nullptr;
    anonVarCounter = 0;
    
    // Force return type to Any for function expressions to ensure compatibility with runtime handlers
    auto oldReturnType = currentReturnType;
    currentReturnType = std::make_shared<Type>(TypeKind::Any);
    
    auto argIt = function->arg_begin();
    if (argIt != function->arg_end()) {
        argIt->setName("context");
        currentContext = &*argIt;
        ++argIt;
    }

    unsigned idx = 0;
    while (argIt != function->arg_end() && idx < node->parameters.size()) {
        auto& param = node->parameters[idx];
        auto& arg = *argIt;
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
            generateDestructuring(&arg, std::make_shared<Type>(TypeKind::Any), param->name.get());
        }
        ++argIt;
        ++idx;
    }
    
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }
    
    currentReturnType = oldReturnType;
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    lastValue = function;
}

} // namespace ts


