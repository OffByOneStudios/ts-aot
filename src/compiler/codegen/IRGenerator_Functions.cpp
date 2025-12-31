#include "IRGenerator.h"
#include "../analysis/Monomorphizer.h"
#include <filesystem>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {
using namespace ast;

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
                argTypes.push_back(getLLVMType(argType));
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
            printf("Function %s not found in module\n", spec.specializedName.c_str());
            continue;
        }

        if (auto methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            if (methodNode->isAbstract || !methodNode->hasBody) continue;
        }

        if (!function->empty()) {
            printf("Function %s already has body\n", spec.specializedName.c_str());
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
        printf("Generating body for: %s isAsync: %d\n", spec.specializedName.c_str(), isAsync);
        SPDLOG_INFO("Generating body for: {} isAsync: {} isGenerator: {}", spec.specializedName, isAsync, isGenerator);

        // Clear function-specific state
        namedValues.clear();
        boxedVariables.clear();
        boxedValues.clear();  // Must clear between specializations to avoid stale pointer reuse
        catchStack.clear();
        finallyStack.clear();
        valueOverrides.clear();

        if (isAsync || isGenerator) {
            generateAsyncFunctionBody(function, spec.node, spec.argTypes, spec.classType, spec.specializedName);
            continue;
        }

        llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        if (debug && spec.node) {
            // Actually, we should have the file name from the compileUnit
            llvm::DIFile* unitFile = compileUnit->getFile();
            
            llvm::DISubroutineType* debugFuncType = diBuilder->createSubroutineType(diBuilder->getOrCreateTypeArray({})); // TODO: proper types
            
            llvm::DISubprogram* sp = diBuilder->createFunction(
                unitFile, spec.specializedName, spec.specializedName, unitFile,
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
            builder->CreateStore(llvm::Constant::getNullValue(function->getReturnType()), currentReturnValueAlloca);
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
                        if (argVal->getType()->isPointerTy()) {
                            nonNullValues.insert(argVal);
                        }
                        if (argType && argType->kind == TypeKind::Class) {
                            concreteTypes[argVal] = std::static_pointer_cast<ClassType>(argType).get();
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

            // Handle JSON imports for module init functions
            if (spec.specializedName.starts_with("__module_init_") && analyzer) {
                // Find the module that corresponds to this init function
                for (auto& [path, mod] : analyzer->modules) {
                    std::string initName = "__module_init_" + std::to_string(std::hash<std::string>{}(path));
                    if (initName == spec.specializedName && mod->ast) {
                        // Process ImportDeclarations for JSON modules
                        for (auto& stmt : mod->ast->body) {
                            if (auto importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get())) {
                                ResolvedModule resolved = analyzer->resolveModule(importDecl->moduleSpecifier);
                                if (resolved.isValid() && resolved.type == ModuleType::JSON) {
                                    auto modIt = analyzer->modules.find(resolved.path);
                                    if (modIt != analyzer->modules.end() && modIt->second->jsonContent.has_value()) {
                                        llvm::Value* jsonVal = generateJsonValue(modIt->second->jsonContent.value());
                                        if (!importDecl->defaultImport.empty()) {
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

            // Pre-scan: find all variables that will be captured by inner closures
            // These variables need to use cells for proper mutable capture semantics
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
                        if (argVal->getType()->isPointerTy()) {
                            nonNullValues.insert(argVal);
                        }
                        if (argType && argType->kind == TypeKind::Class) {
                            concreteTypes[argVal] = std::static_pointer_cast<ClassType>(argType).get();
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
    auto oldNamedValues = namedValues;

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
    
    if (!capturedVars.empty()) {
        std::vector<llvm::Type*> contextFields;
        for (size_t i = 0; i < capturedVars.size(); ++i) {
            contextFields.push_back(builder->getPtrTy()); // All captured values are pointers (cells or boxed values)
            capturedVarIndices[capturedVars[i].name] = static_cast<int>(i);
        }
        closureContextType = llvm::StructType::create(*context, name + "_closure");
        closureContextType->setBody(contextFields);
        
        // Allocate and populate the closure context (in the OUTER function)
        llvm::FunctionType* allocFt = llvm::FunctionType::get(builder->getPtrTy(), { llvm::Type::getInt64Ty(*context) }, false);
        llvm::FunctionCallee allocFn = module->getOrInsertFunction("ts_alloc", allocFt);
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
            if (cellVariables.count(cv.name)) {
                // The captured value is a cell pointer - keep it as a cell variable
                // No need to mark as boxed since access goes through ts_cell_get
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
    } else if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateRet(llvm::ConstantPointerNull::get(builder->getPtrTy()));
    }
    
    currentReturnType = oldReturnType;
    builder->SetInsertPoint(oldBB);
    namedValues = oldNamedValues;
    
    // Always box the function so it can be called via ts_call_N
    // Functions with closure context have a populated context pointer
    // Functions without closure context have a null context pointer
    llvm::FunctionType* makeFnFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy(), builder->getPtrTy() }, false);
    llvm::FunctionCallee makeFnFn = module->getOrInsertFunction("ts_value_make_function", makeFnFt);
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
            boxedValues.insert(&arg);
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
    
    lastValue = boxValue(function, node->inferredType);
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
            boxedValues.insert(&arg);
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
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
        llvm::Value* strVal = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(j.get<std::string>()) });
        return boxValue(strVal, std::make_shared<Type>(TypeKind::String));
    }
    
    if (j.is_array()) {
        // Create array and populate
        llvm::FunctionType* createArrFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createArrFn = module->getOrInsertFunction("ts_array_create", createArrFt);
        llvm::Value* arr = createCall(createArrFt, createArrFn.getCallee(), {});
        
        llvm::FunctionType* pushFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee pushFn = module->getOrInsertFunction("ts_array_push", pushFt);
        
        for (const auto& elem : j) {
            llvm::Value* elemVal = generateJsonValue(elem);
            createCall(pushFt, pushFn.getCallee(), { arr, elemVal });
        }
        
        return boxValue(arr, std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any)));
    }
    
    if (j.is_object()) {
        // Create map and populate
        llvm::FunctionType* createMapFt = llvm::FunctionType::get(builder->getPtrTy(), {}, false);
        llvm::FunctionCallee createMapFn = module->getOrInsertFunction("ts_map_create", createMapFt);
        llvm::Value* map = createCall(createMapFt, createMapFn.getCallee(), {});
        
        llvm::FunctionType* setMapFt = llvm::FunctionType::get(llvm::Type::getVoidTy(*context),
                { builder->getPtrTy(), builder->getPtrTy(), builder->getPtrTy() }, false);
        llvm::FunctionCallee setFn = module->getOrInsertFunction("ts_map_set", setMapFt);
        
        llvm::FunctionType* createStrFt = llvm::FunctionType::get(builder->getPtrTy(), { builder->getPtrTy() }, false);
        llvm::FunctionCallee createStrFn = module->getOrInsertFunction("ts_string_create", createStrFt);
        
        for (auto& [key, val] : j.items()) {
            llvm::Value* keyStr = createCall(createStrFt, createStrFn.getCallee(), { builder->CreateGlobalStringPtr(key) });
            llvm::Value* boxedKey = boxValue(keyStr, std::make_shared<Type>(TypeKind::String));
            llvm::Value* boxedVal = generateJsonValue(val);
            createCall(setMapFt, setFn.getCallee(), { map, boxedKey, boxedVal });
        }
        
        return boxValue(map, std::make_shared<Type>(TypeKind::Object));
    }
    
    return llvm::ConstantPointerNull::get(builder->getPtrTy());
}

} // namespace ts


