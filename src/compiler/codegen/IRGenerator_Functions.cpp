#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#include <filesystem>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

static bool isParamIndexDefaultedPrimitive(const Specialization& spec, size_t argTypeIndex) {
    // Only primitives (non-pointer LLVM types) need boxing to represent `undefined` at runtime.
    // For pointer params, `undefined` can be represented as nullptr and handled without changing ABI.
    ast::Parameter* param = nullptr;

    bool isInstanceMethod = false;
    if (spec.classType && spec.node) {
        if (auto method = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            if (!method->isStatic) isInstanceMethod = true;
        }
    }

    if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
        if (argTypeIndex < funcNode->parameters.size()) {
            param = funcNode->parameters[argTypeIndex].get();
        }
    } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
        if (isInstanceMethod) {
            if (argTypeIndex == 0) return false; // 'this'
            size_t paramIndex = argTypeIndex - 1;
            if (paramIndex < methodNode->parameters.size()) {
                param = methodNode->parameters[paramIndex].get();
            }
        } else {
            if (argTypeIndex < methodNode->parameters.size()) {
                param = methodNode->parameters[argTypeIndex].get();
            }
        }
    }

    if (!param) return false;
    if (!param->initializer) return false;
    if (param->isRest) return false;

    // Primitive check happens at LLVM type level (int/double/bool are non-pointer).
    // Caller will box when callee expects a pointer (TsValue*) argument.
    // NOTE: Any/Union/etc are pointer-typed and do not need this ABI change.
    auto& argType = spec.argTypes[argTypeIndex];
    if (!argType) return false;
    switch (argType->kind) {
        case TypeKind::Int:
        case TypeKind::Double:
        case TypeKind::Boolean:
            return true;
        default:
            return false;
    }
}

void IRGenerator::generatePrototypes(const std::vector<Specialization>& specializations) {
    for (const auto& spec : specializations) {
        if (module->getFunction(spec.specializedName)) continue;

        std::vector<llvm::Type*> argTypes;
        
        // Always add context argument first
        argTypes.push_back(builder->getPtrTy());

        bool isInstanceMethod = false;
        if (spec.classType && spec.node) {
            if (auto method = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
                if (!method->isStatic) isInstanceMethod = true;
            }
        }

        for (size_t i = 0; i < spec.argTypes.size(); ++i) {
            const auto& argType = spec.argTypes[i];
            if (isInstanceMethod && i == 0) {
                // 'this' is always a pointer
                argTypes.push_back(builder->getPtrTy());
            } else {
                if (isParamIndexDefaultedPrimitive(spec, i)) {
                    // Boxed TsValue* so we can represent `undefined` and apply default at runtime.
                    argTypes.push_back(builder->getPtrTy());
                } else {
                    argTypes.push_back(getLLVMType(argType));
                }
            }
        }

        llvm::Type* returnType = getLLVMType(spec.returnType);
        llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, argTypes, false);

        SPDLOG_DEBUG("Creating prototype: {} with {} args", spec.specializedName, argTypes.size());
        llvm::Function* func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, spec.specializedName, module.get());
        addStackProtection(func);

        // Add CFI metadata based on specialized name (which includes signature)
        func->addMetadata(llvm::LLVMContext::MD_type,
            *llvm::MDNode::get(*context, {
                llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
                llvm::MDString::get(*context, spec.specializedName)
            }));
        
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
        if (spec.specializedName.find("lambda") != std::string::npos) {
            SPDLOG_INFO("Skipping body generation for {}", spec.specializedName);
            continue;
        }
        llvm::Function* function = module->getFunction(spec.specializedName);
        if (!function) {
            SPDLOG_WARN("Function {} not found in module", spec.specializedName);
            continue;
        }

        if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            if (methodNode->isAbstract || !methodNode->hasBody) continue;
        }

        if (!function->empty()) {
            SPDLOG_DEBUG("Function {} already has body", spec.specializedName);
            continue;
        }

        // Re-analyze the body with the specialized types to ensure inferredType is correct for THIS specialization
        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            analyzer->analyzeFunctionBody(funcNode, spec.argTypes, spec.typeArguments);
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            analyzer->analyzeMethodBody(methodNode, std::static_pointer_cast<ClassType>(spec.classType), spec.typeArguments);
        }

        bool isAsync = false;
        bool isGenerator = false;
        if (auto funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            isAsync = funcNode->isAsync;
            isGenerator = funcNode->isGenerator;
        } else if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            isAsync = methodNode->isAsync;
            isGenerator = methodNode->isGenerator;
        } else if (auto arrowNode = dynamic_cast<ast::ArrowFunction*>(spec.node)) {
            isAsync = arrowNode->isAsync;
        } else if (auto funcExpr = dynamic_cast<ast::FunctionExpression*>(spec.node)) {
            isAsync = funcExpr->isAsync;
            isGenerator = funcExpr->isGenerator;
        }

        SPDLOG_DEBUG("Generating body for: {} isAsync: {} isGenerator: {}", spec.specializedName, isAsync, isGenerator);

        // Clear function-specific state
        namedValues.clear();
        boxedVariables.clear();
        boxedValues.clear();  // Must clear between specializations to avoid stale pointer reuse
        catchStack.clear();
        finallyStack.clear();
        valueOverrides.clear();
        variableTypes.clear();  // Clear variable type info between function bodies
        boxedElementArrayVars.clear();  // Clear boxed array tracking between function bodies

        if (isAsync || isGenerator) {
            generateAsyncFunctionBody(function, spec.node, spec.argTypes, spec.classType, spec.specializedName);
            continue;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        if (debug && spec.node) {
            // Get the correct DIFile for this function's source file
            llvm::DIFile* funcFile = compileUnit->getFile();  // Default to main file
            if (!spec.node->sourceFile.empty()) {
                funcFile = getOrCreateDIFile(spec.node->sourceFile);
            }
            
            // Build parameter types for debug info
            std::vector<llvm::Metadata*> paramTypes;
            // Return type first
            paramTypes.push_back(createDebugType(spec.returnType));
            // Then parameter types
            for (const auto& argType : spec.argTypes) {
                paramTypes.push_back(createDebugType(argType));
            }
            
            llvm::DISubroutineType* debugFuncType = diBuilder->createSubroutineType(
                diBuilder->getOrCreateTypeArray(paramTypes)
            );
            
            llvm::DISubprogram* sp = diBuilder->createFunction(
                funcFile, spec.specializedName, spec.specializedName, funcFile,
                spec.node->line, debugFuncType, spec.node->line,
                llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
            function->setSubprogram(sp);
            debugScopes.push_back(sp);
            emitLocation(spec.node);
        }

        namedValues.clear();
        nonNullValues.clear();
        checkedAllocas.clear();
        lastValue = nullptr;
        anonVarCounter = 0;

        currentClass = spec.classType;
        finallyStack.clear();
        currentBreakBB = nullptr;
        currentContinueBB = nullptr;

        // Initialize return-related variables
        currentReturnBB = llvm::BasicBlock::Create(*context, "return", function);
        currentShouldReturnAlloca = createEntryBlockAlloca(function, "shouldReturn", builder->getInt1Ty());
        builder->CreateStore(builder->getInt1(false), currentShouldReturnAlloca);

        currentShouldBreakAlloca = createEntryBlockAlloca(function, "shouldBreak", builder->getInt1Ty());
        builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
        currentShouldContinueAlloca = createEntryBlockAlloca(function, "shouldContinue", builder->getInt1Ty());
        builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
        
        currentBreakTargetAlloca = createEntryBlockAlloca(function, "breakTarget", builder->getPtrTy());
        currentContinueTargetAlloca = createEntryBlockAlloca(function, "continueTarget", builder->getPtrTy());
        
        if (!function->getReturnType()->isVoidTy()) {
            currentReturnValueAlloca = createEntryBlockAlloca(function, "returnValue", function->getReturnType());
            // Initialize return value to undefined (not null) for functions returning TsValue*
            if (function->getReturnType()->isPointerTy()) {
                llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
                llvm::Value* undefVal = createCall(undefFt, undefFn.getCallee(), {});
                builder->CreateStore(undefVal, currentReturnValueAlloca);
            } else {
                builder->CreateStore(llvm::Constant::getNullValue(function->getReturnType()), currentReturnValueAlloca);
            }
        } else {
            currentReturnValueAlloca = nullptr;
        }

        // ... rest of the function ...
        // (I'll need to find where the body is generated)
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

            // Pre-scan: find all variables that will be captured by inner closures
            // These variables need to use cells for proper mutable capture semantics
            // IMPORTANT: This must happen BEFORE parameter processing so that captured
            // parameters are also wrapped in cells
            cellVariables.clear();
            cellPointers.clear();
            {
                // Collect all variable names that will be defined in this function
                std::set<std::string> localVarNames;
                for (auto& param : funcNode->parameters) {
                    if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                        localVarNames.insert(id->name);
                    }
                }
                // Also need to scan body for let/const/var declarations
                for (auto& stmt : funcNode->body) {
                    if (auto varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                        // VariableDeclaration has a 'name' field, not 'declarations'
                        if (auto id = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                            localVarNames.insert(id->name);
                        }
                    }
                }
                
                // Now scan for captured variables
                for (auto& stmt : funcNode->body) {
                    collectCapturedVariableNames(stmt.get(), localVarNames, cellVariables);
                }
                
                if (!cellVariables.empty()) {
                    SPDLOG_INFO("Function {} has {} captured variables that will use cells", 
                               spec.specializedName, cellVariables.size());
                    for (const auto& name : cellVariables) {
                        SPDLOG_INFO("  - {}", name);
                    }
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
                    // Check if we received exactly one array argument that matches the rest param type
                    // This happens when the caller used spread: sumAll(...arr)
                    std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                    bool receivedArrayDirectly = false;
                    
                    // Count remaining args - if exactly 1 and it's an array type, use it directly
                    int remainingArgs = 0;
                    for (auto it = argIt; it != function->arg_end(); ++it) remainingArgs++;
                    
                    if (remainingArgs == 1 && argType && argType->kind == TypeKind::Array) {
                        // Received array directly from spread - use it as-is
                        llvm::Value* arr = &*argIt;
                        ++argIt;
                        ++idx;
                        
                        // Don't mark as boxed - this is a raw array, not boxed elements
                        // Pass the array type so codegen knows element types for specialized access
                        generateDestructuring(arr, argType, param->name.get());
                        receivedArrayDirectly = true;
                    }
                    
                    if (!receivedArrayDirectly) {
                        // Normal case: Create an empty array and collect individual args
                        llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                        llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
                        llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

                        llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                                { builder->getPtrTy(), builder->getPtrTy() }, false);
                        llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

                        // Collect all remaining arguments into the array
                        while (argIt != function->arg_end()) {
                            llvm::Value* argVal = &*argIt;
                            // Box the value if it's a primitive
                            std::shared_ptr<Type> boxArgType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                            llvm::Value* boxedVal = boxValue(argVal, boxArgType);
                            createCall(pushFt, pushFn.getCallee(), { arr, boxedVal });
                            ++argIt;
                            ++idx;
                        }

                        // Mark the rest parameter array as containing boxed elements
                        // so that array access won't use specialized (direct) access
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            boxedElementArrayVars.insert(id->name);
                        }

                        generateDestructuring(arr, nullptr, param->name.get());
                    }
                    break; // Rest parameter must be the last one
                } else {
                    if (argIt != function->arg_end()) {
                        auto argVal = &*argIt;
                        std::shared_ptr<Type> argType = (idx < spec.argTypes.size()) ? spec.argTypes[idx] : nullptr;
                        std::shared_ptr<Type> bindType = argType;
                        if (param->initializer) {
                            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                                if (id->inferredType) bindType = id->inferredType;
                            }
                        }
                        if (!bindType) bindType = std::make_shared<Type>(TypeKind::Any);
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            argVal->setName(id->name);
                        }
                        if (argVal->getType()->isPointerTy()) {
                            nonNullValues.insert(argVal);
                        }
                        if (argType && argType->kind == TypeKind::Class) {
                            concreteTypes[argVal] = std::static_pointer_cast<ClassType>(argType).get();
                        }
                        // Mark pointer-type parameters as already boxed if they could be TsValue*
                        // This happens when:
                        // - Function types (passed as boxed TsFunction*)
                        // - Object/Class/Interface types (may be passed as boxed TsValue* from any-typed callers)
                        // This prevents double-boxing when storing to cells
                        // 
                        // EXCEPTION: Module init functions receive the 'module' parameter as a raw pointer,
                        // not boxed. The module object is a TsMap* passed directly, not wrapped in TsValue.
                        bool isModuleInit = (spec.specializedName.find("__module_init_") != std::string::npos);
                        if (argVal->getType()->isPointerTy() && !isModuleInit) {
                            if (argType && (argType->kind == TypeKind::Function ||
                                           argType->kind == TypeKind::Object ||
                                           argType->kind == TypeKind::Class ||
                                           argType->kind == TypeKind::Interface ||
                                           argType->kind == TypeKind::Any)) {
                                boxedValues.insert(argVal);
                            }
                        }
                        // Apply runtime defaulting here so primitives can be unboxed correctly.
                        if (param->initializer && !param->isRest) {
                            llvm::FunctionType* isUndefFt = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                            llvm::FunctionCallee isUndefFn = getRuntimeFunction("ts_value_is_undefined", isUndefFt);

                            llvm::Type* expectedLLVM = bindType ? getLLVMType(bindType) : argVal->getType();
                            bool primitiveDefaulted = (expectedLLVM && !expectedLLVM->isPointerTy() && argVal->getType()->isPointerTy());

                            if (argVal->getType()->isPointerTy() && (primitiveDefaulted || (expectedLLVM && expectedLLVM->isPointerTy()))) {
                                llvm::Value* isUndefined = builder->CreateCall(isUndefFn, { argVal }, "is_undefined");

                                llvm::BasicBlock* defaultBB = llvm::BasicBlock::Create(*context, "default_param", function);
                                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "default_merge", function);
                                llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "default_next", function);

                                builder->CreateCondBr(isUndefined, defaultBB, nextBB);

                                builder->SetInsertPoint(defaultBB);
                                visit(param->initializer.get());
                                llvm::Value* defaultValRaw = lastValue;
                                if (expectedLLVM && defaultValRaw && defaultValRaw->getType() != expectedLLVM) {
                                    defaultValRaw = castValue(defaultValRaw, expectedLLVM);
                                }
                                builder->CreateBr(mergeBB);

                                builder->SetInsertPoint(nextBB);
                                llvm::Value* passedValRaw = argVal;
                                if (primitiveDefaulted) {
                                    passedValRaw = unboxValue(argVal, bindType);
                                    if (expectedLLVM && passedValRaw->getType() != expectedLLVM) {
                                        passedValRaw = castValue(passedValRaw, expectedLLVM);
                                    }
                                }
                                builder->CreateBr(mergeBB);

                                builder->SetInsertPoint(mergeBB);
                                llvm::Type* phiTy = primitiveDefaulted ? expectedLLVM : argVal->getType();
                                llvm::PHINode* phi = builder->CreatePHI(phiTy, 2, "param_val");
                                phi->addIncoming(defaultValRaw, defaultBB);
                                phi->addIncoming(primitiveDefaulted ? passedValRaw : argVal, nextBB);

                                generateDestructuring(phi, bindType, param->name.get());
                            } else {
                                generateDestructuring(argVal, bindType, param->name.get());
                            }
                        } else {
                            // If callee prototype uses boxed args (defaulted primitives), unbox before binding.
                            if (bindType && !getLLVMType(bindType)->isPointerTy() && argVal->getType()->isPointerTy()) {
                                llvm::Value* unboxed = unboxValue(argVal, bindType);
                                generateDestructuring(unboxed, bindType, param->name.get());
                            } else {
                                generateDestructuring(argVal, bindType, param->name.get());
                            }
                        }
                        ++argIt;
                        ++idx;
                    } else {
                        // Handle missing parameters (optional/default)
                        llvm::Value* initVal = nullptr;
                        
                        // Check if we have an initializer (default value)
                        if (param->initializer) {
                            visit(param->initializer.get());
                            initVal = lastValue;
                            std::shared_ptr<Type> bindType = nullptr;
                            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                                bindType = id->inferredType;
                            }
                            if (!bindType) bindType = std::make_shared<Type>(TypeKind::Any);
                            generateDestructuring(initVal, bindType, param->name.get());
                            continue;
                        } else {
                            // Default to undefined (null pointer for now)
                            initVal = llvm::ConstantPointerNull::get(builder->getPtrTy());
                        }
                        generateDestructuring(initVal, nullptr, param->name.get());
                    }
                }
            }
            
            // Add debug info for all parameters after binding
            if (debug && !debugScopes.empty() && funcNode) {
                for (size_t pIdx = 0; pIdx < funcNode->parameters.size(); ++pIdx) {
                    auto param = funcNode->parameters[pIdx].get();
                    if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                        if (namedValues.count(id->name)) {
                            std::shared_ptr<Type> bindType = id->inferredType;
                            if (!bindType) bindType = std::make_shared<Type>(TypeKind::Any);
                            
                            // Get the correct DIFile for the parameter's source file
                            llvm::DIFile* paramFile = compileUnit->getFile();
                            if (!param->sourceFile.empty()) {
                                paramFile = getOrCreateDIFile(param->sourceFile);
                            }
                            
                            llvm::DILocalVariable* debugParam = diBuilder->createParameterVariable(
                                debugScopes.back(),
                                id->name,
                                pIdx + 2, // +1 for 1-based, +1 for context param
                                paramFile,
                                param->line,
                                createDebugType(bindType)
                            );
                            diBuilder->insertDeclare(
                                namedValues[id->name],
                                debugParam,
                                diBuilder->createExpression(),
                                llvm::DILocation::get(*context, param->line, param->column, debugScopes.back()),
                                builder->GetInsertBlock()
                            );
                        }
                    }
                }
            }

            // Handle JSON imports for module init functions
            if (spec.specializedName.starts_with("__module_init_") && analyzer) {
                // Find the module that corresponds to this init function
                for (auto& [path, mod] : analyzer->modules) {
                    std::string initName = "__module_init_" + std::to_string(std::hash<std::string>{}(path));
                    // The specializedName has a type suffix like "_any", so check if it starts with initName
                    if (spec.specializedName.starts_with(initName) && mod->ast) {
                        // Process ImportDeclarations for JSON modules
                        for (auto& stmt : mod->ast->body) {
                            if (auto importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get())) {
                                ResolvedModule resolved = analyzer->resolveModule(importDecl->moduleSpecifier);
                                if (resolved.isValid() && resolved.type == ModuleType::JSON) {
                                    auto modIt = analyzer->modules.find(resolved.path);
                                    if (modIt != analyzer->modules.end() && modIt->second->jsonContent.has_value()) {
                                        llvm::Value* jsonVal = generateJsonValue(modIt->second->jsonContent.value());
                                        if (!importDecl->defaultImport.empty()) {
                                            // Store to global variable (for later loads)
                                            llvm::GlobalVariable* gv = module->getGlobalVariable(importDecl->defaultImport);
                                            if (gv) {
                                                builder->CreateStore(jsonVal, gv);
                                            }
                                            // Also set up local namedValues for immediate use
                                            llvm::AllocaInst* alloca = createEntryBlockAlloca(function, importDecl->defaultImport, builder->getPtrTy());
                                            builder->CreateStore(jsonVal, alloca);
                                            namedValues[importDecl->defaultImport] = alloca;
                                            boxedVariables.insert(importDecl->defaultImport);
                                            SPDLOG_DEBUG("JSON import init: {} -> {}", importDecl->defaultImport, resolved.path);
                                        }
                                    }
                                }
                            }
                        }
                        break;
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
                    llvm::Value* thisVal = &*argIt;
                    llvm::AllocaInst* alloca = createEntryBlockAlloca(function, "this", thisVal->getType());
                    builder->CreateStore(thisVal, alloca);
                    namedValues["this"] = alloca;
                    nonNullValues.insert(thisVal);
                    checkedAllocas.insert(alloca);
                    
                    if (spec.classType) {
                        concreteTypes[alloca] = std::static_pointer_cast<ClassType>(spec.classType).get();
                        concreteTypes[thisVal] = std::static_pointer_cast<ClassType>(spec.classType).get();
                    }
                    
                    ++argIt;
                    idx++;
                }
            } else {
                // Define 'this' for static context
                namedValues["this"] = llvm::ConstantPointerNull::get(builder->getPtrTy());
            }

            // Handle explicit parameters
            for (size_t pIdx = 0; pIdx < methodNode->parameters.size(); ++pIdx) {
                auto param = methodNode->parameters[pIdx].get();

                if (param->isRest) {
                    // Create an empty array
                    llvm::FunctionType* createFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
                    llvm::FunctionCallee createFn = getRuntimeFunction("ts_array_create", createFt);
                    llvm::Value* arr = createCall(createFt, createFn.getCallee(), {});

                    llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                            { builder->getPtrTy(), builder->getPtrTy() }, false);
                    llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);

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
                        std::shared_ptr<Type> bindType = argType;
                        if (param->initializer) {
                            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                                if (id->inferredType) bindType = id->inferredType;
                            }
                        }
                        if (!bindType) bindType = std::make_shared<Type>(TypeKind::Any);
                        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                            argVal->setName(id->name);
                        }
                        if (argVal->getType()->isPointerTy()) {
                            nonNullValues.insert(argVal);
                        }
                        if (argType && argType->kind == TypeKind::Class) {
                            concreteTypes[argVal] = std::static_pointer_cast<ClassType>(argType).get();
                        }
                        // Apply runtime defaulting here so primitives can be unboxed correctly.
                        if (param->initializer && !param->isRest) {
                            llvm::FunctionType* isUndefFt = llvm::FunctionType::get(builder->getInt1Ty(), { builder->getPtrTy() }, false);
                            llvm::FunctionCallee isUndefFn = getRuntimeFunction("ts_value_is_undefined", isUndefFt);

                            llvm::Type* expectedLLVM = bindType ? getLLVMType(bindType) : argVal->getType();
                            bool primitiveDefaulted = (expectedLLVM && !expectedLLVM->isPointerTy() && argVal->getType()->isPointerTy());

                            if (argVal->getType()->isPointerTy() && (primitiveDefaulted || (expectedLLVM && expectedLLVM->isPointerTy()))) {
                                llvm::Value* isUndefined = builder->CreateCall(isUndefFn, { argVal }, "is_undefined");

                                llvm::BasicBlock* defaultBB = llvm::BasicBlock::Create(*context, "default_param", function);
                                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(*context, "default_merge", function);
                                llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(*context, "default_next", function);

                                builder->CreateCondBr(isUndefined, defaultBB, nextBB);

                                builder->SetInsertPoint(defaultBB);
                                visit(param->initializer.get());
                                llvm::Value* defaultValRaw = lastValue;
                                if (expectedLLVM && defaultValRaw && defaultValRaw->getType() != expectedLLVM) {
                                    defaultValRaw = castValue(defaultValRaw, expectedLLVM);
                                }
                                builder->CreateBr(mergeBB);

                                builder->SetInsertPoint(nextBB);
                                llvm::Value* passedValRaw = argVal;
                                if (primitiveDefaulted) {
                                    passedValRaw = unboxValue(argVal, bindType);
                                    if (expectedLLVM && passedValRaw->getType() != expectedLLVM) {
                                        passedValRaw = castValue(passedValRaw, expectedLLVM);
                                    }
                                }
                                builder->CreateBr(mergeBB);

                                builder->SetInsertPoint(mergeBB);
                                llvm::Type* phiTy = primitiveDefaulted ? expectedLLVM : argVal->getType();
                                llvm::PHINode* phi = builder->CreatePHI(phiTy, 2, "param_val");
                                phi->addIncoming(defaultValRaw, defaultBB);
                                phi->addIncoming(primitiveDefaulted ? passedValRaw : argVal, nextBB);

                                generateDestructuring(phi, bindType, param->name.get());
                            } else {
                                generateDestructuring(argVal, bindType, param->name.get());
                            }
                        } else {
                            // If callee prototype uses boxed args (defaulted primitives), unbox before binding.
                            if (bindType && !getLLVMType(bindType)->isPointerTy() && argVal->getType()->isPointerTy()) {
                                llvm::Value* unboxed = unboxValue(argVal, bindType);
                                generateDestructuring(unboxed, bindType, param->name.get());
                            } else {
                                generateDestructuring(argVal, bindType, param->name.get());
                            }
                        }
                        ++argIt;
                        ++idx;
                    } else {
                        // Handle missing parameters (optional/default)
                        llvm::Value* initVal = nullptr;
                        if (param->initializer) {
                            visit(param->initializer.get());
                            initVal = lastValue;
                            std::shared_ptr<Type> bindType = nullptr;
                            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                                bindType = id->inferredType;
                            }
                            if (!bindType) bindType = std::make_shared<Type>(TypeKind::Any);
                            generateDestructuring(initVal, bindType, param->name.get());
                            continue;
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

        llvm::BasicBlock* currentBB = builder->GetInsertBlock();
        if (!currentBB->getTerminator()) {
            builder->CreateBr(currentReturnBB);
        }

        // Emit the actual return block
        if (llvm::pred_begin(currentReturnBB) == llvm::pred_end(currentReturnBB)) {
            currentReturnBB->eraseFromParent();
        } else {
            builder->SetInsertPoint(currentReturnBB);
            llvm::Type* retType = function->getReturnType();
            if (retType->isVoidTy()) {
                builder->CreateRetVoid();
            } else {
                llvm::Value* retVal = builder->CreateLoad(retType, currentReturnValueAlloca);
                if (!retVal) {
                     // Fallback if for some reason it wasn't initialized
                     retVal = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(retType));
                }
                builder->CreateRet(retVal);
            }
        }

        if (debug && spec.node) {
            debugScopes.pop_back();
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
    addStackProtection(function);

    // Add CFI metadata for TsFunction
    function->addMetadata(llvm::LLVMContext::MD_type,
        *llvm::MDNode::get(*context, {
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
            llvm::MDString::get(*context, "TsFunction")
        }));
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();

    struct SavedFunctionState {
        std::map<std::string, llvm::Value*> namedValues;
        std::map<std::string, llvm::Type*> forcedVariableTypes;
        std::map<std::string, std::shared_ptr<Type>> variableTypes;
        std::map<ast::Node*, llvm::Value*> valueOverrides;
        std::set<llvm::Value*> boxedValues;
        std::set<std::string> boxedVariables;
        std::set<std::string> boxedElementArrayVars;
        std::set<std::string> cellVariables;
        std::map<std::string, llvm::Value*> cellPointers;
        std::map<llvm::Value*, std::string> lengthAliases;
        std::string lastLengthArray;

        std::vector<CatchInfo> catchStack;
        std::vector<FinallyInfo> finallyStack;
        std::vector<LoopInfo> loopStack;

        std::set<llvm::Value*> nonNullValues;
        std::set<llvm::Value*> checkedAllocas;

        std::shared_ptr<Type> currentClass;
        std::shared_ptr<Type> currentReturnType;
        llvm::Value* currentContext;

        llvm::BasicBlock* currentBreakBB;
        llvm::BasicBlock* currentContinueBB;
        llvm::BasicBlock* currentReturnBB;
        llvm::Value* currentReturnValueAlloca;
        llvm::Value* currentShouldReturnAlloca;
        llvm::Value* currentShouldBreakAlloca;
        llvm::Value* currentShouldContinueAlloca;
        llvm::Value* currentBreakTargetAlloca;
        llvm::Value* currentContinueTargetAlloca;

        llvm::Value* currentAsyncContext;
        llvm::Value* currentAsyncResumedValue;
        llvm::Value* currentAsyncFrame;
        llvm::StructType* currentAsyncFrameType;
        std::map<std::string, int> currentAsyncFrameMap;
        llvm::BasicBlock* asyncDispatcherBB;
        std::vector<llvm::BasicBlock*> asyncStateBlocks;
        bool currentIsGenerator;
        bool currentIsAsync;

        int anonVarCounter;
    };

    SavedFunctionState saved{
        namedValues,
        forcedVariableTypes,
        variableTypes,
        valueOverrides,
        boxedValues,
        boxedVariables,
        boxedElementArrayVars,
        cellVariables,
        cellPointers,
        lengthAliases,
        lastLengthArray,
        catchStack,
        finallyStack,
        loopStack,
        nonNullValues,
        checkedAllocas,
        currentClass,
        currentReturnType,
        currentContext,
        currentBreakBB,
        currentContinueBB,
        currentReturnBB,
        currentReturnValueAlloca,
        currentShouldReturnAlloca,
        currentShouldBreakAlloca,
        currentShouldContinueAlloca,
        currentBreakTargetAlloca,
        currentContinueTargetAlloca,
        currentAsyncContext,
        currentAsyncResumedValue,
        currentAsyncFrame,
        currentAsyncFrameType,
        currentAsyncFrameMap,
        asyncDispatcherBB,
        asyncStateBlocks,
        currentIsGenerator,
        currentIsAsync,
        anonVarCounter,
    };

    // === CLOSURE CAPTURE: Collect free variables BEFORE clearing namedValues ===
    std::set<std::string> paramNames;
    for (auto& param : node->parameters) {
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramNames.insert(id->name);
        }
    }
    
    std::vector<CapturedVariable> capturedVars;
    if (node->body) {
        collectFreeVariables(node->body.get(), paramNames, capturedVars);
    }
    
    // Create closure context struct type (if we have captures)
    llvm::StructType* closureContextType = nullptr;
    llvm::Value* closureContext = nullptr;
    std::map<std::string, int> capturedVarIndices;
    
    std::set<std::string> capturedCellVarNames;
    if (!capturedVars.empty()) {
        std::vector<llvm::Type*> contextFields;
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            contextFields.push_back(builder->getPtrTy()); // All captured values are pointers (cells or boxed values)
            capturedVarIndices[capturedVars[i].name] = static_cast<int>(i);
        }
        closureContextType = llvm::StructType::create(*context, name + "_closure");
        closureContextType->setBody(contextFields);
        
        // Allocate and populate the closure context (in the OUTER function)
        // Use ts_pool_alloc for faster closure allocation from size-class pools
        llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee allocFn = getRuntimeFunction("ts_pool_alloc", allocFt);
        uint64_t contextSize = module->getDataLayout().getTypeAllocSize(closureContextType);
        closureContext = createCall(allocFt, allocFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), contextSize) });
        
        // Store captured values into the context struct
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            llvm::Value* fieldPtr = builder->CreateStructGEP(closureContextType, closureContext, static_cast<unsigned>(i));
            
            // Check if this is a cell variable
            if (cellVariables.count(capturedVars[i].name)) {
                // For cell variables, just copy the cell pointer
                llvm::Value* cellPtr = builder->CreateLoad(builder->getPtrTy(), capturedVars[i].value);
                builder->CreateStore(cellPtr, fieldPtr);
                capturedCellVarNames.insert(capturedVars[i].name);
                SPDLOG_INFO("  Captured cell variable {} at index {}", capturedVars[i].name, i);
                continue;
            }
            
            // Determine the type to load from the outer scope's alloca
            llvm::Type* loadType = builder->getPtrTy(); // default
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(capturedVars[i].value)) {
                loadType = alloca->getAllocatedType();
            }
            
            // Load the current value from the outer scope's alloca
            llvm::Value* outerValue = builder->CreateLoad(loadType, capturedVars[i].value);
            
            // Box it if not already a pointer (i.e., if it's a primitive type)
            // BUT: don't box function pointers that are already boxed TsValue*
            bool shouldBox = !loadType->isPointerTy() || !boxedValues.count(outerValue);
            
            // Function type variables that are already pointers are boxed TsValue* - don't box again
            if (capturedVars[i].type && capturedVars[i].type->kind == TypeKind::Function && loadType->isPointerTy()) {
                shouldBox = false;
            }
            
            if (shouldBox) {
                outerValue = boxValue(outerValue, capturedVars[i].type);
            }
            builder->CreateStore(outerValue, fieldPtr);
        }
        
        SPDLOG_INFO("Arrow function {} captures {} variables", name, capturedVars.size());
        // Track that this function has closures - needed to prevent direct call optimization
        functionsWithClosures.insert(name);
    }

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
        // Restore outer function state
        namedValues = saved.namedValues;
        forcedVariableTypes = saved.forcedVariableTypes;
        variableTypes = saved.variableTypes;
        valueOverrides = saved.valueOverrides;
        boxedValues = saved.boxedValues;
        boxedVariables = saved.boxedVariables;
        boxedElementArrayVars = saved.boxedElementArrayVars;
        cellVariables = saved.cellVariables;
        cellPointers = saved.cellPointers;
        lengthAliases = saved.lengthAliases;
        lastLengthArray = saved.lastLengthArray;
        catchStack = saved.catchStack;
        finallyStack = saved.finallyStack;
        loopStack = saved.loopStack;
        nonNullValues = saved.nonNullValues;
        checkedAllocas = saved.checkedAllocas;
        currentClass = saved.currentClass;
        currentReturnType = saved.currentReturnType;
        currentContext = saved.currentContext;
        currentBreakBB = saved.currentBreakBB;
        currentContinueBB = saved.currentContinueBB;
        currentReturnBB = saved.currentReturnBB;
        currentReturnValueAlloca = saved.currentReturnValueAlloca;
        currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
        currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
        currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
        currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
        currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
        currentAsyncContext = saved.currentAsyncContext;
        currentAsyncResumedValue = saved.currentAsyncResumedValue;
        currentAsyncFrame = saved.currentAsyncFrame;
        currentAsyncFrameType = saved.currentAsyncFrameType;
        currentAsyncFrameMap = saved.currentAsyncFrameMap;
        asyncDispatcherBB = saved.asyncDispatcherBB;
        asyncStateBlocks = saved.asyncStateBlocks;
        currentIsGenerator = saved.currentIsGenerator;
        currentIsAsync = saved.currentIsAsync;
        anonVarCounter = saved.anonVarCounter;

        lastValue = boxValue(function, node->inferredType);
        return;
    }
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);

    // Clear function-specific state for this nested function body
    namedValues.clear();
    forcedVariableTypes.clear();
    variableTypes.clear();
    valueOverrides.clear();
    boxedVariables.clear();
    boxedValues.clear();
    boxedElementArrayVars.clear();
    cellVariables.clear();
    cellPointers.clear();
    lengthAliases.clear();
    lastLengthArray.clear();
    catchStack.clear();
    finallyStack.clear();
    loopStack.clear();
    nonNullValues.clear();
    checkedAllocas.clear();
    lastValue = nullptr;
    anonVarCounter = 0;
    
    // Force return type to Any for arrow functions to ensure compatibility with runtime handlers
    auto oldReturnType = currentReturnType;
    currentReturnType = std::make_shared<Type>(TypeKind::Any);

    // Initialize per-function return/break/continue state so try/finally and loops do not
    // accidentally branch to blocks from an enclosing function.
    currentBreakBB = nullptr;
    currentContinueBB = nullptr;
    // Create return block but DON'T add it to function yet - only insert if it gets used
    currentReturnBB = llvm::BasicBlock::Create(*context, "return");
    currentShouldReturnAlloca = createEntryBlockAlloca(function, "shouldReturn", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldReturnAlloca);
    currentShouldBreakAlloca = createEntryBlockAlloca(function, "shouldBreak", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
    currentShouldContinueAlloca = createEntryBlockAlloca(function, "shouldContinue", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    currentBreakTargetAlloca = createEntryBlockAlloca(function, "breakTarget", builder->getPtrTy());
    currentContinueTargetAlloca = createEntryBlockAlloca(function, "continueTarget", builder->getPtrTy());
    currentReturnValueAlloca = createEntryBlockAlloca(function, "returnValue", builder->getPtrTy());
    // Initialize return value to undefined (not null) to avoid null pointer crashes
    {
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
        llvm::Value* undefVal = createCall(undefFt, undefFn.getCallee(), {});
        builder->CreateStore(undefVal, currentReturnValueAlloca);
    }
    
    auto argIt = function->arg_begin();
    llvm::Value* contextArg = nullptr;
    if (argIt != function->arg_end()) {
        argIt->setName("context");
        contextArg = &*argIt;
        currentContext = contextArg;
        ++argIt;
    }

    // === CLOSURE CAPTURE: Extract captured values from context at function entry ===
    if (!capturedVars.empty() && closureContextType && contextArg) {
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            const auto& cv = capturedVars[i];
            // Create a local alloca for this captured variable
            llvm::AllocaInst* alloca = createEntryBlockAlloca(function, cv.name, builder->getPtrTy());
            // Load the value from the context struct
            llvm::Value* fieldPtr = builder->CreateStructGEP(closureContextType, contextArg, static_cast<unsigned>(i));
            llvm::Value* capturedValue = builder->CreateLoad(builder->getPtrTy(), fieldPtr);
            // Store into local alloca
            builder->CreateStore(capturedValue, alloca);
            // Add to namedValues so the body can use it
            namedValues[cv.name] = alloca;
            
            // Check if this was a cell variable in the outer scope
            if (capturedCellVarNames.count(cv.name)) {
                // The captured value is a cell pointer - keep it as a cell variable
                // No need to mark as boxed since access goes through ts_cell_get
                cellVariables.insert(cv.name);
                cellPointers[cv.name] = capturedValue;
                SPDLOG_INFO("  Extracted cell variable {} at index {}", cv.name, i);
            } else {
                // Regular captured value - mark as boxed
                boxedValues.insert(capturedValue);
                boxedVariables.insert(cv.name);
                SPDLOG_INFO("  Extracted captured var {} at index {}", cv.name, i);
            }
        }
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
        
        // Mark as boxed and unbox the argument
        boxedValues.insert(&arg);
        llvm::Value* unboxedArg = unboxValue(&arg, paramType);
        generateDestructuring(unboxedArg, paramType, param->name.get());
        
        ++argIt;
        ++idx;
    }
    
    if (node->body) {
        SPDLOG_DEBUG("Visiting arrow function body: {} at {}", node->body->getKind(), (void*)node->body.get());
        visit(node->body.get());
    } else {
        SPDLOG_ERROR("Arrow function body is NULL! Node at {}", (void*)node);
    }
    
    if (node->body && node->body->getKind() != "BlockStatement" && lastValue && !builder->GetInsertBlock()->getTerminator()) {
        // Box the return value
        std::shared_ptr<Type> returnType = funcType ? funcType->returnType : std::make_shared<Type>(TypeKind::Any);
        llvm::Value* boxedRet = boxValue(lastValue, returnType);
        builder->CreateRet(boxedRet);
    } else {
        llvm::BasicBlock* currentBB = builder->GetInsertBlock();
        if (currentBB && !currentBB->getTerminator()) {
            // Need to use the return block - insert it into function now
            if (!currentReturnBB->getParent()) {
                currentReturnBB->insertInto(function);
            }
            builder->CreateBr(currentReturnBB);
        }

        // Emit unified return block if it was actually used (has predecessors)
        if (currentReturnBB->getParent()) {
            // Block was inserted, so check if it has predecessors
            if (llvm::pred_begin(currentReturnBB) == llvm::pred_end(currentReturnBB)) {
                // No predecessors - remove it
                currentReturnBB->eraseFromParent();
            } else {
                // Has predecessors - add terminator
                builder->SetInsertPoint(currentReturnBB);
                llvm::Value* retVal = builder->CreateLoad(builder->getPtrTy(), currentReturnValueAlloca);
                builder->CreateRet(retVal);
            }
        } else {
            // Block was never inserted - delete the unused object
            delete currentReturnBB;
        }
    }
    
    currentReturnType = oldReturnType;
    builder->SetInsertPoint(oldBB);

    // Restore outer function state
    namedValues = saved.namedValues;
    forcedVariableTypes = saved.forcedVariableTypes;
    variableTypes = saved.variableTypes;
    valueOverrides = saved.valueOverrides;
    boxedValues = saved.boxedValues;
    boxedVariables = saved.boxedVariables;
    boxedElementArrayVars = saved.boxedElementArrayVars;
    cellVariables = saved.cellVariables;
    cellPointers = saved.cellPointers;
    lengthAliases = saved.lengthAliases;
    lastLengthArray = saved.lastLengthArray;
    catchStack = saved.catchStack;
    finallyStack = saved.finallyStack;
    loopStack = saved.loopStack;
    nonNullValues = saved.nonNullValues;
    checkedAllocas = saved.checkedAllocas;
    currentClass = saved.currentClass;
    currentReturnType = saved.currentReturnType;
    currentContext = saved.currentContext;
    currentBreakBB = saved.currentBreakBB;
    currentContinueBB = saved.currentContinueBB;
    currentReturnBB = saved.currentReturnBB;
    currentReturnValueAlloca = saved.currentReturnValueAlloca;
    currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
    currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
    currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
    currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
    currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
    currentAsyncContext = saved.currentAsyncContext;
    currentAsyncResumedValue = saved.currentAsyncResumedValue;
    currentAsyncFrame = saved.currentAsyncFrame;
    currentAsyncFrameType = saved.currentAsyncFrameType;
    currentAsyncFrameMap = saved.currentAsyncFrameMap;
    asyncDispatcherBB = saved.asyncDispatcherBB;
    asyncStateBlocks = saved.asyncStateBlocks;
    currentIsGenerator = saved.currentIsGenerator;
    currentIsAsync = saved.currentIsAsync;
    anonVarCounter = saved.anonVarCounter;
    
    // Always box the function so it can be called via ts_call_N
    // Functions with closure context have a populated context pointer
    // Functions without closure context have a null context pointer
    llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee makeFnFn = getRuntimeFunction("ts_value_make_function", makeFnFt);
    llvm::Value* contextPtr = closureContext ? closureContext : llvm::ConstantPointerNull::get(builder->getPtrTy());
    lastValue = createCall(makeFnFt, makeFnFn.getCallee(), { function, contextPtr });
    boxedValues.insert(lastValue);
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
    addStackProtection(function);

    // Add CFI metadata for TsFunction
    function->addMetadata(llvm::LLVMContext::MD_type,
        *llvm::MDNode::get(*context, {
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
            llvm::MDString::get(*context, "TsFunction")
        }));
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();

    // === CLOSURE CAPTURE: Collect free variables BEFORE clearing namedValues ===
    std::set<std::string> paramNames;
    for (auto& param : node->parameters) {
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramNames.insert(id->name);
        }
    }
    
    std::vector<CapturedVariable> capturedVars;
    for (auto& stmt : node->body) {
        collectFreeVariables(stmt.get(), paramNames, capturedVars);
    }
    
    // Create closure context struct type (if we have captures)
    llvm::StructType* closureContextType = nullptr;
    llvm::Value* closureContext = nullptr;
    std::map<std::string, int> capturedVarIndices;
    
    std::set<std::string> capturedCellVarNames;
    if (!capturedVars.empty()) {
        std::vector<llvm::Type*> contextFields;
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            contextFields.push_back(builder->getPtrTy()); // All captured values are pointers (cells or boxed values)
            capturedVarIndices[capturedVars[i].name] = static_cast<int>(i);
        }
        closureContextType = llvm::StructType::create(*context, name + "_closure");
        closureContextType->setBody(contextFields);
        
        // Allocate and populate the closure context (in the OUTER function)
        // Use ts_pool_alloc for faster closure allocation from size-class pools
        llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee allocFn = getRuntimeFunction("ts_pool_alloc", allocFt);
        uint64_t contextSize = module->getDataLayout().getTypeAllocSize(closureContextType);
        closureContext = createCall(allocFt, allocFn.getCallee(), { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), contextSize) });
        
        // Store captured values into the context struct
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            llvm::Value* fieldPtr = builder->CreateStructGEP(closureContextType, closureContext, static_cast<unsigned>(i));
            
            // Check if this is a cell variable
            if (cellVariables.count(capturedVars[i].name)) {
                // For cell variables, just copy the cell pointer
                llvm::Value* cellPtr = builder->CreateLoad(builder->getPtrTy(), capturedVars[i].value);
                builder->CreateStore(cellPtr, fieldPtr);
                capturedCellVarNames.insert(capturedVars[i].name);
                SPDLOG_INFO("  Captured cell variable {} at index {}", capturedVars[i].name, i);
                continue;
            }
            
            // Determine the type to load from the outer scope's alloca
            llvm::Type* loadType = builder->getPtrTy(); // default
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(capturedVars[i].value)) {
                loadType = alloca->getAllocatedType();
            }
            
            // Load the current value from the outer scope's alloca
            llvm::Value* outerValue = builder->CreateLoad(loadType, capturedVars[i].value);
            
            // Box it if not already a pointer (i.e., if it's a primitive type)
            // BUT: don't box function pointers that are already boxed TsValue*
            bool shouldBox = !loadType->isPointerTy() || !boxedValues.count(outerValue);
            
            // Function type variables that are already pointers are boxed TsValue* - don't box again
            if (capturedVars[i].type && capturedVars[i].type->kind == TypeKind::Function && loadType->isPointerTy()) {
                shouldBox = false;
            }
            
            if (shouldBox) {
                outerValue = boxValue(outerValue, capturedVars[i].type);
            }
            builder->CreateStore(outerValue, fieldPtr);
        }
        
        SPDLOG_INFO("FunctionExpression {} captures {} variables", name, capturedVars.size());
        // Track that this function has closures - needed to prevent direct call optimization
        functionsWithClosures.insert(name);
    }

    struct SavedFunctionState {
        std::map<std::string, llvm::Value*> namedValues;
        std::map<std::string, llvm::Type*> forcedVariableTypes;
        std::map<std::string, std::shared_ptr<Type>> variableTypes;
        std::map<ast::Node*, llvm::Value*> valueOverrides;
        std::set<llvm::Value*> boxedValues;
        std::set<std::string> boxedVariables;
        std::set<std::string> boxedElementArrayVars;
        std::set<std::string> cellVariables;
        std::map<std::string, llvm::Value*> cellPointers;
        std::map<llvm::Value*, std::string> lengthAliases;
        std::string lastLengthArray;

        std::vector<CatchInfo> catchStack;
        std::vector<FinallyInfo> finallyStack;
        std::vector<LoopInfo> loopStack;

        std::set<llvm::Value*> nonNullValues;
        std::set<llvm::Value*> checkedAllocas;

        std::shared_ptr<Type> currentClass;
        std::shared_ptr<Type> currentReturnType;
        llvm::Value* currentContext;

        llvm::BasicBlock* currentBreakBB;
        llvm::BasicBlock* currentContinueBB;
        llvm::BasicBlock* currentReturnBB;
        llvm::Value* currentReturnValueAlloca;
        llvm::Value* currentShouldReturnAlloca;
        llvm::Value* currentShouldBreakAlloca;
        llvm::Value* currentShouldContinueAlloca;
        llvm::Value* currentBreakTargetAlloca;
        llvm::Value* currentContinueTargetAlloca;

        llvm::Value* currentAsyncContext;
        llvm::Value* currentAsyncResumedValue;
        llvm::Value* currentAsyncFrame;
        llvm::StructType* currentAsyncFrameType;
        std::map<std::string, int> currentAsyncFrameMap;
        llvm::BasicBlock* asyncDispatcherBB;
        std::vector<llvm::BasicBlock*> asyncStateBlocks;
        bool currentIsGenerator;
        bool currentIsAsync;

        int anonVarCounter;
    };

    SavedFunctionState saved{
        namedValues,
        forcedVariableTypes,
        variableTypes,
        valueOverrides,
        boxedValues,
        boxedVariables,
        boxedElementArrayVars,
        cellVariables,
        cellPointers,
        lengthAliases,
        lastLengthArray,
        catchStack,
        finallyStack,
        loopStack,
        nonNullValues,
        checkedAllocas,
        currentClass,
        currentReturnType,
        currentContext,
        currentBreakBB,
        currentContinueBB,
        currentReturnBB,
        currentReturnValueAlloca,
        currentShouldReturnAlloca,
        currentShouldBreakAlloca,
        currentShouldContinueAlloca,
        currentBreakTargetAlloca,
        currentContinueTargetAlloca,
        currentAsyncContext,
        currentAsyncResumedValue,
        currentAsyncFrame,
        currentAsyncFrameType,
        currentAsyncFrameMap,
        asyncDispatcherBB,
        asyncStateBlocks,
        currentIsGenerator,
        currentIsAsync,
        anonVarCounter,
    };

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
        // Restore outer function state
        namedValues = saved.namedValues;
        forcedVariableTypes = saved.forcedVariableTypes;
        variableTypes = saved.variableTypes;
        valueOverrides = saved.valueOverrides;
        boxedValues = saved.boxedValues;
        boxedVariables = saved.boxedVariables;
        boxedElementArrayVars = saved.boxedElementArrayVars;
        cellVariables = saved.cellVariables;
        cellPointers = saved.cellPointers;
        lengthAliases = saved.lengthAliases;
        lastLengthArray = saved.lastLengthArray;
        catchStack = saved.catchStack;
        finallyStack = saved.finallyStack;
        loopStack = saved.loopStack;
        nonNullValues = saved.nonNullValues;
        checkedAllocas = saved.checkedAllocas;
        currentClass = saved.currentClass;
        currentReturnType = saved.currentReturnType;
        currentContext = saved.currentContext;
        currentBreakBB = saved.currentBreakBB;
        currentContinueBB = saved.currentContinueBB;
        currentReturnBB = saved.currentReturnBB;
        currentReturnValueAlloca = saved.currentReturnValueAlloca;
        currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
        currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
        currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
        currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
        currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
        currentAsyncContext = saved.currentAsyncContext;
        currentAsyncResumedValue = saved.currentAsyncResumedValue;
        currentAsyncFrame = saved.currentAsyncFrame;
        currentAsyncFrameType = saved.currentAsyncFrameType;
        currentAsyncFrameMap = saved.currentAsyncFrameMap;
        asyncDispatcherBB = saved.asyncDispatcherBB;
        asyncStateBlocks = saved.asyncStateBlocks;
        currentIsGenerator = saved.currentIsGenerator;
        currentIsAsync = saved.currentIsAsync;
        anonVarCounter = saved.anonVarCounter;

        lastValue = boxValue(function, node->inferredType);
        return;
    }
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);

    // Clear function-specific state for this nested function body
    namedValues.clear();
    forcedVariableTypes.clear();
    variableTypes.clear();
    valueOverrides.clear();
    boxedVariables.clear();
    boxedValues.clear();
    boxedElementArrayVars.clear();
    cellVariables.clear();
    cellPointers.clear();
    lengthAliases.clear();
    lastLengthArray.clear();
    catchStack.clear();
    finallyStack.clear();
    loopStack.clear();
    nonNullValues.clear();
    checkedAllocas.clear();
    lastValue = nullptr;
    anonVarCounter = 0;
    
    // Force return type to Any for function expressions to ensure compatibility with runtime handlers
    auto oldReturnType = currentReturnType;
    currentReturnType = std::make_shared<Type>(TypeKind::Any);

    // Initialize per-function return/break/continue state so try/finally and loops do not
    // accidentally branch to blocks from an enclosing function.
    currentBreakBB = nullptr;
    currentContinueBB = nullptr;
    currentReturnBB = llvm::BasicBlock::Create(*context, "return", function);
    currentShouldReturnAlloca = createEntryBlockAlloca(function, "shouldReturn", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldReturnAlloca);
    currentShouldBreakAlloca = createEntryBlockAlloca(function, "shouldBreak", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
    currentShouldContinueAlloca = createEntryBlockAlloca(function, "shouldContinue", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    currentBreakTargetAlloca = createEntryBlockAlloca(function, "breakTarget", builder->getPtrTy());
    currentContinueTargetAlloca = createEntryBlockAlloca(function, "continueTarget", builder->getPtrTy());
    currentReturnValueAlloca = createEntryBlockAlloca(function, "returnValue", builder->getPtrTy());
    // Initialize return value to undefined (not null) to avoid null pointer crashes
    {
        llvm::FunctionType* undefFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee undefFn = getRuntimeFunction("ts_value_make_undefined", undefFt);
        llvm::Value* undefVal = createCall(undefFt, undefFn.getCallee(), {});
        builder->CreateStore(undefVal, currentReturnValueAlloca);
    }
    
    auto argIt = function->arg_begin();
    llvm::Value* contextArg = nullptr;
    if (argIt != function->arg_end()) {
        argIt->setName("context");
        contextArg = &*argIt;
        currentContext = contextArg;
        ++argIt;
    }

    // === CLOSURE CAPTURE: Extract captured values from context at function entry ===
    if (!capturedVars.empty() && closureContextType && contextArg) {
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            const auto& cv = capturedVars[i];
            // Create a local alloca for this captured variable
            llvm::AllocaInst* alloca = createEntryBlockAlloca(function, cv.name, builder->getPtrTy());
            // Load the value from the context struct
            llvm::Value* fieldPtr = builder->CreateStructGEP(closureContextType, contextArg, static_cast<unsigned>(i));
            llvm::Value* capturedValue = builder->CreateLoad(builder->getPtrTy(), fieldPtr);
            // Store into local alloca
            builder->CreateStore(capturedValue, alloca);
            // Add to namedValues so the body can use it
            namedValues[cv.name] = alloca;
            
            // Check if this was a cell variable in the outer scope
            if (capturedCellVarNames.count(cv.name)) {
                // The captured value is a cell pointer - keep it as a cell variable
                // No need to mark as boxed since access goes through ts_cell_get
                cellVariables.insert(cv.name);
                cellPointers[cv.name] = capturedValue;
                SPDLOG_INFO("  FuncExpr: Extracted cell variable {} at index {}", cv.name, i);
            } else {
                // Regular captured value - mark as boxed
                boxedValues.insert(capturedValue);
                boxedVariables.insert(cv.name);
                SPDLOG_INFO("  FuncExpr: Extracted captured var {} at index {}", cv.name, i);
            }
        }
    }

    unsigned idx = 0;
    while (argIt != function->arg_end() && idx < node->parameters.size()) {
        auto& param = node->parameters[idx];
        auto& arg = *argIt;
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            arg.setName(id->name);
            boxedValues.insert(&arg);
            generateDestructuring(&arg, std::make_shared<Type>(TypeKind::Any), param->name.get());
        }
        ++argIt;
        ++idx;
    }
    
    // Pre-pass: Hoist function declarations (JavaScript hoisting behavior)
    // This ensures function names are available before their declaration is visited
    hoistFunctionDeclarations(node->body, function);
    
    // Pre-pass: Hoist variable declarations
    // This ensures that all var declarations in this function scope are in namedValues
    // BEFORE we visit any nested functions that might reference them.
    // This fixes closure capture for variables declared after the closure textually.
    hoistVariableDeclarations(node->body, function);
    
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    llvm::BasicBlock* currentBB = builder->GetInsertBlock();
    if (currentBB && !currentBB->getTerminator()) {
        builder->CreateBr(currentReturnBB);
    }

    // Emit unified return block if it has predecessors
    if (llvm::pred_begin(currentReturnBB) == llvm::pred_end(currentReturnBB)) {
        currentReturnBB->eraseFromParent();
    } else {
        builder->SetInsertPoint(currentReturnBB);
        llvm::Value* retVal = builder->CreateLoad(builder->getPtrTy(), currentReturnValueAlloca);
        builder->CreateRet(retVal);
    }
    
    currentReturnType = oldReturnType;
    builder->SetInsertPoint(oldBB);

    // Restore outer function state
    namedValues = saved.namedValues;
    forcedVariableTypes = saved.forcedVariableTypes;
    variableTypes = saved.variableTypes;
    valueOverrides = saved.valueOverrides;
    boxedValues = saved.boxedValues;
    boxedVariables = saved.boxedVariables;
    boxedElementArrayVars = saved.boxedElementArrayVars;
    cellVariables = saved.cellVariables;
    cellPointers = saved.cellPointers;
    lengthAliases = saved.lengthAliases;
    lastLengthArray = saved.lastLengthArray;
    catchStack = saved.catchStack;
    finallyStack = saved.finallyStack;
    loopStack = saved.loopStack;
    nonNullValues = saved.nonNullValues;
    checkedAllocas = saved.checkedAllocas;
    currentClass = saved.currentClass;
    currentReturnType = saved.currentReturnType;
    currentContext = saved.currentContext;
    currentBreakBB = saved.currentBreakBB;
    currentContinueBB = saved.currentContinueBB;
    currentReturnBB = saved.currentReturnBB;
    currentReturnValueAlloca = saved.currentReturnValueAlloca;
    currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
    currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
    currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
    currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
    currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
    currentAsyncContext = saved.currentAsyncContext;
    currentAsyncResumedValue = saved.currentAsyncResumedValue;
    currentAsyncFrame = saved.currentAsyncFrame;
    currentAsyncFrameType = saved.currentAsyncFrameType;
    currentAsyncFrameMap = saved.currentAsyncFrameMap;
    asyncDispatcherBB = saved.asyncDispatcherBB;
    asyncStateBlocks = saved.asyncStateBlocks;
    currentIsGenerator = saved.currentIsGenerator;
    currentIsAsync = saved.currentIsAsync;
    anonVarCounter = saved.anonVarCounter;

    // Always box the function so it can be called via ts_call_N
    // Functions with closure context have a populated context pointer
    // Functions without closure context have a null context pointer
    llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee makeFnFn = getRuntimeFunction("ts_value_make_function", makeFnFt);
    llvm::Value* contextPtr = closureContext ? closureContext : llvm::ConstantPointerNull::get(builder->getPtrTy());
    lastValue = createCall(makeFnFt, makeFnFn.getCallee(), { function, contextPtr });
    boxedValues.insert(lastValue);
}

void IRGenerator::visitMethodDefinition(ast::MethodDefinition* node) {
    static int methodCounter = 0;
    std::string name = "method_" + std::to_string(methodCounter++);
    
    std::vector<llvm::Type*> argTypes;
    argTypes.push_back(builder->getPtrTy()); // context first
    for (auto& param : node->parameters) {
        argTypes.push_back(builder->getPtrTy()); // TsValue*
    }
    
    llvm::Type* retType = builder->getPtrTy(); // TsValue*
    
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::Function* function = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name, module.get());
    addStackProtection(function);

    // Add CFI metadata for TsFunction
    function->addMetadata(llvm::LLVMContext::MD_type,
        *llvm::MDNode::get(*context, {
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0)),
            llvm::MDString::get(*context, "TsFunction")
        }));
    
    llvm::BasicBlock* oldBB = builder->GetInsertBlock();

    struct SavedFunctionState {
        std::map<std::string, llvm::Value*> namedValues;
        std::map<std::string, llvm::Type*> forcedVariableTypes;
        std::map<std::string, std::shared_ptr<Type>> variableTypes;
        std::map<ast::Node*, llvm::Value*> valueOverrides;
        std::set<llvm::Value*> boxedValues;
        std::set<std::string> boxedVariables;
        std::set<std::string> boxedElementArrayVars;
        std::set<std::string> cellVariables;
        std::map<std::string, llvm::Value*> cellPointers;
        std::map<llvm::Value*, std::string> lengthAliases;
        std::string lastLengthArray;

        std::vector<CatchInfo> catchStack;
        std::vector<FinallyInfo> finallyStack;
        std::vector<LoopInfo> loopStack;

        std::set<llvm::Value*> nonNullValues;
        std::set<llvm::Value*> checkedAllocas;

        std::shared_ptr<Type> currentClass;
        std::shared_ptr<Type> currentReturnType;
        llvm::Value* currentContext;

        llvm::BasicBlock* currentBreakBB;
        llvm::BasicBlock* currentContinueBB;
        llvm::BasicBlock* currentReturnBB;
        llvm::Value* currentReturnValueAlloca;
        llvm::Value* currentShouldReturnAlloca;
        llvm::Value* currentShouldBreakAlloca;
        llvm::Value* currentShouldContinueAlloca;
        llvm::Value* currentBreakTargetAlloca;
        llvm::Value* currentContinueTargetAlloca;

        llvm::Value* currentAsyncContext;
        llvm::Value* currentAsyncResumedValue;
        llvm::Value* currentAsyncFrame;
        llvm::StructType* currentAsyncFrameType;
        std::map<std::string, int> currentAsyncFrameMap;
        llvm::BasicBlock* asyncDispatcherBB;
        std::vector<llvm::BasicBlock*> asyncStateBlocks;
        bool currentIsGenerator;
        bool currentIsAsync;

        int anonVarCounter;
    };

    SavedFunctionState saved{
        namedValues,
        forcedVariableTypes,
        variableTypes,
        valueOverrides,
        boxedValues,
        boxedVariables,
        boxedElementArrayVars,
        cellVariables,
        cellPointers,
        lengthAliases,
        lastLengthArray,
        catchStack,
        finallyStack,
        loopStack,
        nonNullValues,
        checkedAllocas,
        currentClass,
        currentReturnType,
        currentContext,
        currentBreakBB,
        currentContinueBB,
        currentReturnBB,
        currentReturnValueAlloca,
        currentShouldReturnAlloca,
        currentShouldBreakAlloca,
        currentShouldContinueAlloca,
        currentBreakTargetAlloca,
        currentContinueTargetAlloca,
        currentAsyncContext,
        currentAsyncResumedValue,
        currentAsyncFrame,
        currentAsyncFrameType,
        currentAsyncFrameMap,
        asyncDispatcherBB,
        asyncStateBlocks,
        currentIsGenerator,
        currentIsAsync,
        anonVarCounter,
    };

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
        // Restore outer function state
        namedValues = saved.namedValues;
        forcedVariableTypes = saved.forcedVariableTypes;
        variableTypes = saved.variableTypes;
        valueOverrides = saved.valueOverrides;
        boxedValues = saved.boxedValues;
        boxedVariables = saved.boxedVariables;
        boxedElementArrayVars = saved.boxedElementArrayVars;
        cellVariables = saved.cellVariables;
        cellPointers = saved.cellPointers;
        lengthAliases = saved.lengthAliases;
        lastLengthArray = saved.lastLengthArray;
        catchStack = saved.catchStack;
        finallyStack = saved.finallyStack;
        loopStack = saved.loopStack;
        nonNullValues = saved.nonNullValues;
        checkedAllocas = saved.checkedAllocas;
        currentClass = saved.currentClass;
        currentReturnType = saved.currentReturnType;
        currentContext = saved.currentContext;
        currentBreakBB = saved.currentBreakBB;
        currentContinueBB = saved.currentContinueBB;
        currentReturnBB = saved.currentReturnBB;
        currentReturnValueAlloca = saved.currentReturnValueAlloca;
        currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
        currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
        currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
        currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
        currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
        currentAsyncContext = saved.currentAsyncContext;
        currentAsyncResumedValue = saved.currentAsyncResumedValue;
        currentAsyncFrame = saved.currentAsyncFrame;
        currentAsyncFrameType = saved.currentAsyncFrameType;
        currentAsyncFrameMap = saved.currentAsyncFrameMap;
        asyncDispatcherBB = saved.asyncDispatcherBB;
        asyncStateBlocks = saved.asyncStateBlocks;
        currentIsGenerator = saved.currentIsGenerator;
        currentIsAsync = saved.currentIsAsync;
        anonVarCounter = saved.anonVarCounter;

        lastValue = boxValue(function, node->inferredType);
        return;
    }
    
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
    builder->SetInsertPoint(bb);

    // Clear function-specific state for this nested function body
    namedValues.clear();
    forcedVariableTypes.clear();
    variableTypes.clear();
    valueOverrides.clear();
    boxedVariables.clear();
    boxedValues.clear();
    boxedElementArrayVars.clear();
    cellVariables.clear();
    cellPointers.clear();
    lengthAliases.clear();
    lastLengthArray.clear();
    catchStack.clear();
    finallyStack.clear();
    loopStack.clear();
    nonNullValues.clear();
    checkedAllocas.clear();
    lastValue = nullptr;
    anonVarCounter = 0;
    
    // Force return type to Any for function expressions to ensure compatibility with runtime handlers
    auto oldReturnType = currentReturnType;
    currentReturnType = std::make_shared<Type>(TypeKind::Any);

    // Initialize per-function return/break/continue state
    currentBreakBB = nullptr;
    currentContinueBB = nullptr;
    currentReturnBB = llvm::BasicBlock::Create(*context, "return", function);
    currentShouldReturnAlloca = createEntryBlockAlloca(function, "shouldReturn", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldReturnAlloca);
    currentShouldBreakAlloca = createEntryBlockAlloca(function, "shouldBreak", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldBreakAlloca);
    currentShouldContinueAlloca = createEntryBlockAlloca(function, "shouldContinue", builder->getInt1Ty());
    builder->CreateStore(builder->getInt1(false), currentShouldContinueAlloca);
    currentBreakTargetAlloca = createEntryBlockAlloca(function, "breakTarget", builder->getPtrTy());
    currentContinueTargetAlloca = createEntryBlockAlloca(function, "continueTarget", builder->getPtrTy());
    currentReturnValueAlloca = createEntryBlockAlloca(function, "returnValue", builder->getPtrTy());
    builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), currentReturnValueAlloca);
    
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
            boxedValues.insert(&arg);
            generateDestructuring(&arg, std::make_shared<Type>(TypeKind::Any), param->name.get());
        }
        ++argIt;
        ++idx;
    }
    
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    llvm::BasicBlock* currentBB = builder->GetInsertBlock();
    if (currentBB && !currentBB->getTerminator()) {
        builder->CreateBr(currentReturnBB);
    }

    if (llvm::pred_begin(currentReturnBB) == llvm::pred_end(currentReturnBB)) {
        currentReturnBB->eraseFromParent();
    } else {
        builder->SetInsertPoint(currentReturnBB);
        llvm::Value* retVal = builder->CreateLoad(builder->getPtrTy(), currentReturnValueAlloca);
        builder->CreateRet(retVal);
    }
    
    currentReturnType = oldReturnType;
    builder->SetInsertPoint(oldBB);

    // Restore outer function state
    namedValues = saved.namedValues;
    forcedVariableTypes = saved.forcedVariableTypes;
    variableTypes = saved.variableTypes;
    valueOverrides = saved.valueOverrides;
    boxedValues = saved.boxedValues;
    boxedVariables = saved.boxedVariables;
    boxedElementArrayVars = saved.boxedElementArrayVars;
    cellVariables = saved.cellVariables;
    cellPointers = saved.cellPointers;
    lengthAliases = saved.lengthAliases;
    lastLengthArray = saved.lastLengthArray;
    catchStack = saved.catchStack;
    finallyStack = saved.finallyStack;
    loopStack = saved.loopStack;
    nonNullValues = saved.nonNullValues;
    checkedAllocas = saved.checkedAllocas;
    currentClass = saved.currentClass;
    currentReturnType = saved.currentReturnType;
    currentContext = saved.currentContext;
    currentBreakBB = saved.currentBreakBB;
    currentContinueBB = saved.currentContinueBB;
    currentReturnBB = saved.currentReturnBB;
    currentReturnValueAlloca = saved.currentReturnValueAlloca;
    currentShouldReturnAlloca = saved.currentShouldReturnAlloca;
    currentShouldBreakAlloca = saved.currentShouldBreakAlloca;
    currentShouldContinueAlloca = saved.currentShouldContinueAlloca;
    currentBreakTargetAlloca = saved.currentBreakTargetAlloca;
    currentContinueTargetAlloca = saved.currentContinueTargetAlloca;
    currentAsyncContext = saved.currentAsyncContext;
    currentAsyncResumedValue = saved.currentAsyncResumedValue;
    currentAsyncFrame = saved.currentAsyncFrame;
    currentAsyncFrameType = saved.currentAsyncFrameType;
    currentAsyncFrameMap = saved.currentAsyncFrameMap;
    asyncDispatcherBB = saved.asyncDispatcherBB;
    asyncStateBlocks = saved.asyncStateBlocks;
    currentIsGenerator = saved.currentIsGenerator;
    currentIsAsync = saved.currentIsAsync;
    anonVarCounter = saved.anonVarCounter;
    
    lastValue = boxValue(function, node->inferredType);
}

void IRGenerator::visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) {
    // Handled in visitObjectLiteralExpression
    lastValue = nullptr;
}

llvm::Value* IRGenerator::generateJsonValue(const nlohmann::json& j) {
    using json = nlohmann::json;
    
    if (j.is_null()) {
        return llvm::ConstantPointerNull::get(builder->getPtrTy());
    }
    
    if (j.is_boolean()) {
        llvm::Value* val = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), j.get<bool>());
        return boxValue(val, std::make_shared<Type>(TypeKind::Boolean));
    }
    
    if (j.is_number_integer()) {
        llvm::Value* val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), j.get<int64_t>());
        return boxValue(val, std::make_shared<Type>(TypeKind::Int));
    }
    
    if (j.is_number_float()) {
        llvm::Value* val = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), j.get<double>());
        return boxValue(val, std::make_shared<Type>(TypeKind::Double));
    }
    
    if (j.is_string()) {
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        llvm::Value* strVal = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(j.get<std::string>()) });
        return boxValue(strVal, std::make_shared<Type>(TypeKind::String));
    }
    
    if (j.is_array()) {
        // Create array and populate
        llvm::FunctionType* createArrFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createArrFn = getRuntimeFunction("ts_array_create", createArrFt);
        llvm::Value* arr = createCall(createArrFt, createArrFn.getCallee(), {});
        
        llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pushFn = getRuntimeFunction("ts_array_push", pushFt);
        
        for (const auto& elem : j) {
            llvm::Value* elemVal = generateJsonValue(elem);
            createCall(pushFt, pushFn.getCallee(), { arr, elemVal });
        }
        
        return boxValue(arr, std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any)));
    }
    
    if (j.is_object()) {
        // Create map and populate
        llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createMapFn = getRuntimeFunction("ts_map_create", createMapFt);
        llvm::Value* map = createCall(createMapFt, createMapFn.getCallee(), {});
        
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = getRuntimeFunction("ts_string_create", createStrFt);
        
        for (auto& [key, val] : j.items()) {
            llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(key) });
            llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
            llvm::Value* boxedVal = generateJsonValue(val);
            // Use inline map set operation
            emitInlineMapSet(map, boxedKey, boxedVal);
        }
        
        return boxValue(map, std::make_shared<Type>(TypeKind::Object));
    }
    
    return llvm::ConstantPointerNull::get(builder->getPtrTy());
}

// JavaScript function hoisting - pre-register function declarations before body execution
// This implements the hoisting semantics where function declarations are accessible
// throughout their containing scope, even before the declaration appears textually.
// 
// Two-phase approach:
// Phase 1: Create allocas for all function names and initialize to null (this function)
// Phase 2: The actual function bodies are generated during normal statement processing
void IRGenerator::hoistFunctionDeclarations(const std::vector<ast::StmtPtr>& stmts, llvm::Function* enclosingFn) {
    for (auto& stmt : stmts) {
        if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            if (funcDecl->name.empty()) continue;
            
            // Skip if already registered
            if (namedValues.count(funcDecl->name)) {
                continue;
            }
            
            // Phase 1: Just create an alloca and initialize to null
            // The actual function will be generated during normal body processing
            llvm::AllocaInst* alloca = createEntryBlockAlloca(enclosingFn, funcDecl->name, builder->getPtrTy());
            builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), alloca);
            namedValues[funcDecl->name] = alloca;
            boxedVariables.insert(funcDecl->name);
        }
    }
}

// JavaScript variable hoisting - pre-register all var declarations before body execution
// This ensures that when nested function expressions are compiled, all variables from
// the enclosing function scope are already in namedValues and can be captured.
// Without this, a nested function that references a var declared AFTER it (textually)
// would fail to capture it because namedValues wouldn't contain it yet.
void IRGenerator::hoistVariableDeclarations(const std::vector<ast::StmtPtr>& stmts, llvm::Function* enclosingFn) {
    // Collect all variables declared in this scope
    std::vector<VariableInfo> vars;
    for (auto& stmt : stmts) {
        collectVariables(stmt.get(), vars);
    }
    
    // Create allocas for each variable (hoisting to function entry block)
    for (auto& var : vars) {
        if (var.name.empty()) continue;
        
        // Skip if already registered (e.g., from function hoisting or parameters)
        if (namedValues.count(var.name)) {
            continue;
        }
        
        // Determine the type for the alloca
        llvm::Type* allocaType = var.llvmType;
        if (!allocaType) {
            allocaType = builder->getPtrTy();  // Default to pointer type
        }
        
        // Check if this variable will be captured by a nested closure
        // If so, use pointer type (cells will be created later)
        if (cellVariables.count(var.name)) {
            allocaType = builder->getPtrTy();
        }
        
        // Create alloca and initialize to undefined/null
        llvm::AllocaInst* alloca = createEntryBlockAlloca(enclosingFn, var.name, allocaType);
        if (allocaType->isPointerTy()) {
            builder->CreateStore(llvm::ConstantPointerNull::get(builder->getPtrTy()), alloca);
        } else if (allocaType->isDoubleTy()) {
            builder->CreateStore(llvm::ConstantFP::get(allocaType, 0.0), alloca);
        } else if (allocaType->isIntegerTy()) {
            builder->CreateStore(llvm::ConstantInt::get(allocaType, 0), alloca);
        }
        
        namedValues[var.name] = alloca;
        
        // Track the type for later use
        if (var.type) {
            variableTypes[var.name] = var.type;
        }
        
        SPDLOG_DEBUG("Hoisted variable {} in function {}", var.name, enclosingFn->getName().str());
    }
}

} // namespace ts


