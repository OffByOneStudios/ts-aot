#include "Monomorphizer.h"
#include "Analyzer.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <set>
#include <iostream>

namespace ts {

static ast::Expression* unwrapParens(ast::Expression* expr) {
    while (expr) {
        auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(expr);
        if (!paren || !paren->expression) break;
        expr = paren->expression.get();
    }
    return expr;
}

static std::unique_ptr<ast::Statement> makeConsoleLogStatement(const std::string& msg) {
    auto logCall = std::make_unique<ast::CallExpression>();
    auto logAccess = std::make_unique<ast::PropertyAccessExpression>();
    auto consoleId = std::make_unique<ast::Identifier>();
    consoleId->name = "console";
    logAccess->expression = std::move(consoleId);
    logAccess->name = "log";
    logCall->callee = std::move(logAccess);

    auto msgLit = std::make_unique<ast::StringLiteral>();
    msgLit->value = msg;
    logCall->arguments.push_back(std::move(msgLit));

    auto logStmt = std::make_unique<ast::ExpressionStatement>();
    logStmt->expression = std::move(logCall);
    return logStmt;
}

Monomorphizer::Monomorphizer() {}

void Monomorphizer::monomorphize(ast::Program* program, Analyzer& analyzer) {
    const auto& usages = analyzer.getFunctionUsages();
    std::vector<std::string> moduleInitFunctions;

    SPDLOG_INFO("Monomorphizing {} modules", analyzer.moduleOrder.size());
    for (const auto& path : analyzer.moduleOrder) {
        SPDLOG_INFO("Processing module: {}", path);
        auto it = analyzer.modules.find(path);
        if (it == analyzer.modules.end()) {
            // fmt::print("Module NOT FOUND in analyzer.modules: {}\n", path);
            continue;
        }
        auto module = it->second;
        if (!module->ast) {
            // fmt::print("Module has no AST!\n");
            continue;
        }

        auto moduleInit = std::make_unique<ast::FunctionDeclaration>();
        std::string initName = "__module_init_" + std::to_string(std::hash<std::string>{}(path));
        moduleInit->name = initName;
        moduleInit->isAsync = module->isAsync;
        moduleInit->sourceFile = path;  // Set source file for debug info
        moduleInit->line = 1;  // Module init starts at line 1
        if (module->isAsync) {
            SPDLOG_DEBUG("Module {} is async, marking init as async", path);
        }
        moduleInitFunctions.push_back(initName);

        // Track which module this synthetic init belongs to so analysis/codegen can
        // apply the correct (e.g., untyped JS) semantic mode when re-analyzing bodies.
        analyzer.syntheticFunctionOwners[initName] = path;
        SPDLOG_INFO("Registered synthetic owner: {} -> {}", initName, path);

        // Add 'module' parameter to module init
        auto moduleParam = std::make_unique<ast::Parameter>();
        auto paramName = std::make_unique<ast::Identifier>();
        paramName->name = "module";
        moduleParam->name = std::move(paramName);
        moduleParam->type = "any";
        moduleInit->parameters.push_back(std::move(moduleParam));

        // Prepend exports declaration for CommonJS support
        // const exports = module.exports;
        {
            auto exportsDecl = std::make_unique<ast::VariableDeclaration>();
            auto exportsName = std::make_unique<ast::Identifier>();
            exportsName->name = "exports";
            exportsDecl->name = std::move(exportsName);
            exportsDecl->type = "any";
            
            auto moduleAccess = std::make_unique<ast::PropertyAccessExpression>();
            auto moduleRef = std::make_unique<ast::Identifier>();
            moduleRef->name = "module";
            moduleAccess->expression = std::move(moduleRef);
            moduleAccess->name = "exports";
            exportsDecl->initializer = std::move(moduleAccess);
            moduleInit->body.push_back(std::move(exportsDecl));
        }

        const bool isLodashModule =
            (path.find("node_modules\\lodash\\lodash.js") != std::string::npos) ||
            (path.size() >= 9 && path.substr(path.size() - 9) == "lodash.js");

        // For JavaScript modules, FunctionDeclarations should be moved to moduleInit
        // because JavaScript function hoisting happens at runtime within the module scope.
        // For TypeScript, FunctionDeclarations are kept in newBody and processed by the monomorphizer.
        const bool isJavaScriptModule = (module->type == ModuleType::UntypedJavaScript || 
                                         module->type == ModuleType::TypedJavaScript);

        std::vector<std::unique_ptr<ast::Statement>> newBody;
        for (auto& stmt : module->ast->body) {
            std::string kind = stmt->getKind();
            // For JavaScript: move FunctionDeclarations to moduleInit (runtime hoisting)
            // For TypeScript: keep FunctionDeclarations in newBody (compile-time processing)
            bool keepInNewBody = false;
            if (kind == "ClassDeclaration" ||
                kind == "InterfaceDeclaration" ||
                kind == "ImportDeclaration" ||
                kind == "ExportDeclaration") {
                keepInNewBody = true;
            } else if (kind == "FunctionDeclaration") {
                // TypeScript: keep for monomorphization
                // JavaScript: move to moduleInit for runtime hoisting
                keepInNewBody = !isJavaScriptModule;
            }
            
            if (keepInNewBody) {
                newBody.push_back(std::move(stmt));
                if (isLodashModule) {
                    fmt::print("    -> Moved to newBody\n");
                }
            } else {
                // Move everything else (VariableDeclarations, ExpressionStatements, etc.) to module init
                moduleInit->body.push_back(std::move(stmt));
            }
        }

        // Targeted debug markers for lodash: DISABLED for now
        if (false && isLodashModule) {
            // Add marker showing how many statements we're about to traverse
            std::string sizeMsg = "lodash_moduleInit_stmts=" + std::to_string(moduleInit->body.size());
            moduleInit->body.insert(moduleInit->body.begin() + 1, makeConsoleLogStatement(sizeMsg));
            
            for (size_t stmtIndex = 0; stmtIndex < moduleInit->body.size(); ++stmtIndex) {
                auto* exprStmt = dynamic_cast<ast::ExpressionStatement*>(moduleInit->body[stmtIndex].get());
                if (!exprStmt || !exprStmt->expression) continue;

                auto* call = dynamic_cast<ast::CallExpression*>(exprStmt->expression.get());
                if (!call || !call->callee) continue;

                auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(call->callee.get());
                if (!prop || prop->name != "call" || !prop->expression) continue;

                // The IIFE callee is commonly parenthesized.
                auto* innerExpr = unwrapParens(prop->expression.get());
                auto* fn = dynamic_cast<ast::FunctionExpression*>(innerExpr);
                if (!fn) continue;

                // Found IIFE - add markers before and after the call
                std::string beforeMarker = "lodash_before_iife_at_stmt=" + std::to_string(stmtIndex);
                moduleInit->body.insert(moduleInit->body.begin() + stmtIndex, makeConsoleLogStatement(beforeMarker));
                
                // After inserting before marker, the IIFE is now at stmtIndex + 1
                // Insert after marker at stmtIndex + 2
                moduleInit->body.insert(moduleInit->body.begin() + stmtIndex + 2, 
                                       makeConsoleLogStatement("lodash_after_iife"));
                
                // DON'T add markers inside the IIFE - they might corrupt something
                
                break;
            }
        }

        // Always return module.exports at the end of module init for CommonJS support
        {
            auto finalRet = std::make_unique<ast::ReturnStatement>();
            auto finalModuleAccess = std::make_unique<ast::PropertyAccessExpression>();
            auto finalModuleRef = std::make_unique<ast::Identifier>();
            finalModuleRef->name = "module";
            finalModuleAccess->expression = std::move(finalModuleRef);
            finalModuleAccess->name = "exports";
            finalRet->expression = std::move(finalModuleAccess);
            moduleInit->body.push_back(std::move(finalRet));
        }

        // fmt::print("Module init now has {} statements\n", moduleInit->body.size());
        module->ast->body = std::move(newBody);

        std::string mangledName = Monomorphizer::generateMangledName(initName, { std::make_shared<Type>(TypeKind::Any) }, {});

        // Some later phases may refer to the synthetic function by its mangled name.
        analyzer.syntheticFunctionOwners[mangledName] = path;
        SPDLOG_INFO("Registered synthetic owner: {} -> {}", mangledName, path);
        SPDLOG_INFO("Adding specialization for module init: {} -> {}", initName, mangledName);
        Specialization spec;
        spec.originalName = initName;
        spec.specializedName = mangledName;
        spec.argTypes = { std::make_shared<Type>(TypeKind::Any) };
        spec.returnType = std::make_shared<Type>(TypeKind::Any);
        spec.node = moduleInit.get();
        
        // Set line number to 1 for synthetic module init
        moduleInit->line = 1;
        moduleInit->column = 1;
        
        auto ft = std::make_shared<FunctionType>();
        ft->returnType = std::make_shared<Type>(TypeKind::Any);
        ft->paramTypes = { std::make_shared<Type>(TypeKind::Any) };
        analyzer.getSymbolTable().define(initName, ft);

        specializations.push_back(spec);
        syntheticFunctions.push_back(std::move(moduleInit));
    }

    // Special case for the program passed to monomorphize if it's not in analyzer.modules
    // (Though it should be now)
    
    // Create synthetic user_main that calls all module inits
    // This will be named __synthetic_user_main to avoid conflicts with user-defined user_main
    auto userMain = std::make_unique<ast::FunctionDeclaration>();
    
    // Check if user defined their own user_main function
    ast::FunctionDeclaration* userDefinedMain = findFunction(analyzer, "user_main");
    if (userDefinedMain) {
        // Rename our synthetic function to avoid conflict
        userMain->name = "__synthetic_user_main";
    } else {
        userMain->name = "user_main";
    }
    
    bool anyAsync = false;
    for (const auto& path : analyzer.moduleOrder) {
        auto it = analyzer.modules.find(path);
        if (it != analyzer.modules.end() && it->second->isAsync) {
            anyAsync = true;
            break;
        }
    }
    userMain->isAsync = anyAsync;
    userMain->line = 1;
    userMain->column = 1;

    auto userMainFt = std::make_shared<FunctionType>();
    userMainFt->returnType = std::make_shared<Type>(TypeKind::Any);
    analyzer.getSymbolTable().define("user_main", userMainFt);

    // Phase 1: Create and register all module objects
    std::vector<std::string> moduleObjNames;
    for (size_t i = 0; i < moduleInitFunctions.size(); ++i) {
        const auto& path = analyzer.moduleOrder[i];
        std::string modObjName = "__module_obj_" + std::to_string(i);
        moduleObjNames.push_back(modObjName);

        // let __module_obj_i = { exports: {} };
        auto modDecl = std::make_unique<ast::VariableDeclaration>();
        auto modName = std::make_unique<ast::Identifier>();
        modName->name = modObjName;
        modDecl->name = std::move(modName);
        modDecl->type = "any";

        auto objLit = std::make_unique<ast::ObjectLiteralExpression>();
        auto prop = std::make_unique<ast::PropertyAssignment>();
        prop->name = "exports";
        prop->initializer = std::make_unique<ast::ObjectLiteralExpression>();
        objLit->properties.push_back(std::move(prop));
        modDecl->initializer = std::move(objLit);
        userMain->body.push_back(std::move(modDecl));

        // ts_module_register(path, __module_obj_i);
        auto registerCall = std::make_unique<ast::CallExpression>();
        auto registerId = std::make_unique<ast::Identifier>();
        registerId->name = "ts_module_register";
        registerCall->callee = std::move(registerId);
        
        auto pathLit = std::make_unique<ast::StringLiteral>();
        pathLit->value = path;
        registerCall->arguments.push_back(std::move(pathLit));
        
        auto modRef = std::make_unique<ast::Identifier>();
        modRef->name = modObjName;
        registerCall->arguments.push_back(std::move(modRef));
        
        auto registerStmt = std::make_unique<ast::ExpressionStatement>();
        registerStmt->expression = std::move(registerCall);
        userMain->body.push_back(std::move(registerStmt));
    }

    // Phase 2: Initialize all modules
    for (size_t i = 0; i < moduleInitFunctions.size(); ++i) {
        const auto& initName = moduleInitFunctions[i];
        const auto& path = analyzer.moduleOrder[i];
        const auto& modObjName = moduleObjNames[i];
        
        // Create a variable to hold the module init result
        std::string resVarName = "__module_res_" + std::to_string(i);
        auto resDecl = std::make_unique<ast::VariableDeclaration>();
        auto resName = std::make_unique<ast::Identifier>();
        resName->name = resVarName;
        resDecl->name = std::move(resName);

        auto call = std::make_unique<ast::CallExpression>();
        auto callId = std::make_unique<ast::Identifier>();
        callId->name = initName;
        auto initFt = std::make_shared<FunctionType>();
        initFt->returnType = std::make_shared<Type>(TypeKind::Any);
        // Add parameter type for 'module'
        initFt->paramTypes = { std::make_shared<Type>(TypeKind::Any) };
        callId->inferredType = initFt;
        call->callee = std::move(callId);

        // Pass the module object as argument
        auto modRef = std::make_unique<ast::Identifier>();
        modRef->name = modObjName;
        call->arguments.push_back(std::move(modRef));
        
        auto it = analyzer.modules.find(path);
        if (it != analyzer.modules.end() && it->second->isAsync) {
             auto promiseClass = std::static_pointer_cast<ClassType>(analyzer.getSymbolTable().lookupType("Promise"));
             auto wrapped = std::make_shared<ClassType>("Promise");
             wrapped->methods = promiseClass->methods;
             wrapped->typeArguments = { std::make_shared<Type>(TypeKind::Any) };
             call->inferredType = wrapped;
        } else {
             call->inferredType = std::make_shared<Type>(TypeKind::Any);
        }

        std::unique_ptr<ast::Expression> finalExpr;
        if (anyAsync) {
            auto awaitExpr = std::make_unique<ast::AwaitExpression>();
            awaitExpr->expression = std::move(call);
            awaitExpr->inferredType = std::make_shared<Type>(TypeKind::Any);
            finalExpr = std::move(awaitExpr);
        } else {
            finalExpr = std::move(call);
        }

        resDecl->initializer = std::move(finalExpr);
        userMain->body.push_back(std::move(resDecl));

        // If this is the last module init and there's NO user-defined user_main,
        // make it a return statement.
        if (i == moduleInitFunctions.size() - 1 && !userDefinedMain) {
            auto stmt = std::make_unique<ast::ReturnStatement>();
            auto finalResRef = std::make_unique<ast::Identifier>();
            finalResRef->name = resVarName;
            stmt->expression = std::move(finalResRef);
            userMain->body.push_back(std::move(stmt));
        }
    }
    
    // If user defined their own user_main, call it after all module inits
    if (userDefinedMain) {
        auto call = std::make_unique<ast::CallExpression>();
        auto callId = std::make_unique<ast::Identifier>();
        callId->name = "user_main";
        call->callee = std::move(callId);
        // Use the actual return type from the user-defined function
        if (userDefinedMain->returnType.empty()) {
            call->inferredType = std::make_shared<Type>(TypeKind::Void);
        } else {
            call->inferredType = analyzer.parseType(userDefinedMain->returnType, analyzer.getSymbolTable());
        }
        
        auto stmt = std::make_unique<ast::ReturnStatement>();
        stmt->expression = std::move(call);
        userMain->body.push_back(std::move(stmt));
        
        // Also create a specialization for the user-defined user_main
        Specialization userMainSpec;
        userMainSpec.originalName = "user_main";
        userMainSpec.specializedName = "user_main";
        userMainSpec.argTypes = {};
        // Use the actual return type from the user-defined function
        if (userDefinedMain->returnType.empty()) {
            userMainSpec.returnType = std::make_shared<Type>(TypeKind::Void);
        } else {
            userMainSpec.returnType = analyzer.parseType(userDefinedMain->returnType, analyzer.getSymbolTable());
        }
        userMainSpec.node = userDefinedMain;
        specializations.push_back(userMainSpec);
    }

    Specialization mainSpec;
    mainSpec.originalName = userMain->name;
    mainSpec.specializedName = userMain->name;
    mainSpec.argTypes = {};
    mainSpec.returnType = std::make_shared<Type>(TypeKind::Any);
    mainSpec.node = userMain.get();
    specializations.push_back(mainSpec);

    std::set<std::string> processedSpecializations;
    bool changed = true;
    while (changed) {
        changed = false;
        auto currentUsages = analyzer.getFunctionUsages(); // Copy the map to avoid iterator invalidation

        for (const auto& [name, calls] : currentUsages) {
            ast::FunctionDeclaration* funcNode = findFunction(analyzer, name);
            if (!funcNode) continue;

            for (const auto& call : calls) {
                std::string mangled = generateMangledName(name, call.argTypes, call.typeArguments);
                if (processedSpecializations.count(mangled)) continue;
                
                processedSpecializations.insert(mangled);
                changed = true;

                Specialization spec;
                spec.originalName = name;
                spec.specializedName = mangled;
                
                if (spec.specializedName == "main") {
                    spec.specializedName = "__ts_main";
                }

                spec.argTypes = call.argTypes;
                spec.typeArguments = call.typeArguments;
                spec.node = funcNode;
                
                // Infer return type - this might record NEW usages in analyzer.functionUsages
                spec.returnType = analyzer.analyzeFunctionBody(funcNode, call.argTypes, call.typeArguments);
                
                specializations.push_back(spec);
            }
        }
    }

    // Always ensure main is monomorphized if it exists
    ast::FunctionDeclaration* mainFunc = findFunction(analyzer, "main");
    if (mainFunc) {
        bool alreadyMonomorphized = false;
        for (const auto& spec : specializations) {
            if (spec.originalName == "main" && spec.argTypes.empty()) {
                alreadyMonomorphized = true;
                break;
            }
        }

        if (!alreadyMonomorphized) {
            Specialization spec;
            spec.originalName = "main";
            spec.specializedName = "__ts_main";
            spec.argTypes = {};
            spec.node = mainFunc;
            spec.returnType = analyzer.analyzeFunctionBody(mainFunc, {});
            specializations.push_back(spec);
        }
    }

    // Process Class Methods
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        for (const auto& stmt : module->ast->body) {
            if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                auto classType = analyzer.getSymbolTable().lookupType(cls->name);
                if (!classType || classType->kind != TypeKind::Class) continue;
                auto ct = std::static_pointer_cast<ClassType>(classType);

                // Skip generic classes here; they are handled via classUsages
                if (!ct->typeParameters.empty()) continue;

                for (const auto& member : cls->members) {
                    if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                        if (method->isAbstract) continue;

                        Specialization spec;
                        spec.originalName = method->name;
                        spec.node = method;
                        spec.classType = ct;

                        if (method->isStatic) {
                            std::string methodName = Analyzer::manglePrivateName(method->name, cls->name);
                            spec.specializedName = cls->name + "_static_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                            if (ct->staticMethods.count(methodName)) {
                                auto methodType = ct->staticMethods[methodName];
                                spec.argTypes = methodType->paramTypes;
                                spec.returnType = methodType->returnType;
                            } else if (ct->getters.count(methodName)) {
                                auto methodType = ct->getters[methodName];
                                spec.argTypes = methodType->paramTypes;
                                spec.returnType = methodType->returnType;
                            } else if (ct->setters.count(methodName)) {
                                auto methodType = ct->setters[methodName];
                                spec.argTypes = methodType->paramTypes;
                                spec.returnType = methodType->returnType;
                            } else {
                                spec.returnType = std::make_shared<Type>(TypeKind::Void);
                            }
                        } else {
                            std::string methodName = Analyzer::manglePrivateName(method->name, cls->name);
                            spec.specializedName = cls->name + "_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                            // Construct argTypes: [ClassType, explicitParams...]
                            spec.argTypes.push_back(ct);
                            
                            if (method->name == "constructor" && !ct->constructorOverloads.empty()) {
                                auto ctorType = ct->constructorOverloads[0];
                                spec.argTypes.insert(spec.argTypes.end(), 
                                    ctorType->paramTypes.begin(), ctorType->paramTypes.end());
                                spec.returnType = std::make_shared<Type>(TypeKind::Void);
                            } else if (ct->methods.count(methodName)) {
                                auto methodType = ct->methods[methodName];
                                spec.argTypes.insert(spec.argTypes.end(), 
                                    methodType->paramTypes.begin(), methodType->paramTypes.end());
                                spec.returnType = methodType->returnType;
                            } else if (ct->getters.count(methodName)) {
                                auto methodType = ct->getters[methodName];
                                spec.argTypes.insert(spec.argTypes.end(), 
                                    methodType->paramTypes.begin(), methodType->paramTypes.end());
                                spec.returnType = methodType->returnType;
                            } else if (ct->setters.count(methodName)) {
                                auto methodType = ct->setters[methodName];
                                spec.argTypes.insert(spec.argTypes.end(), 
                                    methodType->paramTypes.begin(), methodType->paramTypes.end());
                                spec.returnType = methodType->returnType;
                            } else {
                                spec.returnType = std::make_shared<Type>(TypeKind::Void);
                            }
                        }

                        specializations.push_back(spec);
                    }
                }
            }
        }
    }

    // Process Class Expression Methods
    for (const auto& expr : analyzer.expressions) {
        auto* classExpr = dynamic_cast<ast::ClassExpression*>(expr);
        if (!classExpr) continue;

        if (!classExpr->inferredType || classExpr->inferredType->kind != TypeKind::Class) continue;
        auto ct = std::static_pointer_cast<ClassType>(classExpr->inferredType);

        // Skip generic classes here
        if (!ct->typeParameters.empty()) continue;

        SPDLOG_DEBUG("Processing class expression: {}", ct->name);

        for (const auto& member : classExpr->members) {
            if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (method->isAbstract) continue;

                Specialization spec;
                spec.originalName = method->name;
                spec.node = method;
                spec.classType = ct;

                if (method->isStatic) {
                    std::string methodName = Analyzer::manglePrivateName(method->name, ct->name);
                    spec.specializedName = ct->name + "_static_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                    if (ct->staticMethods.count(methodName)) {
                        auto methodType = ct->staticMethods[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else if (ct->getters.count(methodName)) {
                        auto methodType = ct->getters[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else if (ct->setters.count(methodName)) {
                        auto methodType = ct->setters[methodName];
                        spec.argTypes = methodType->paramTypes;
                        spec.returnType = methodType->returnType;
                    } else {
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    }
                } else {
                    std::string methodName = Analyzer::manglePrivateName(method->name, ct->name);
                    spec.specializedName = ct->name + "_" + (method->isGetter ? "get_" : (method->isSetter ? "set_" : "")) + methodName;
                    // Construct argTypes: [ClassType, explicitParams...]
                    spec.argTypes.push_back(ct);

                    if (method->name == "constructor" && !ct->constructorOverloads.empty()) {
                        auto ctorType = ct->constructorOverloads[0];
                        spec.argTypes.insert(spec.argTypes.end(),
                            ctorType->paramTypes.begin(), ctorType->paramTypes.end());
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    } else if (ct->methods.count(methodName)) {
                        auto methodType = ct->methods[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else if (ct->getters.count(methodName)) {
                        auto methodType = ct->getters[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else if (ct->setters.count(methodName)) {
                        auto methodType = ct->setters[methodName];
                        spec.argTypes.insert(spec.argTypes.end(),
                            methodType->paramTypes.begin(), methodType->paramTypes.end());
                        spec.returnType = methodType->returnType;
                    } else {
                        spec.returnType = std::make_shared<Type>(TypeKind::Void);
                    }
                }

                SPDLOG_DEBUG("Adding specialization for class expression method: {}", spec.specializedName);
                specializations.push_back(spec);
            }
        }
    }

    const auto& classUsages = analyzer.getClassUsages();
    for (const auto& [name, instances] : classUsages) {
        ast::ClassDeclaration* classNode = findClass(analyzer, name);
        if (!classNode) continue;

        // Deduplicate by type argument strings
        std::map<std::string, std::vector<std::shared_ptr<Type>>> uniqueInstances;
        for (const auto& inst : instances) {
            std::string key;
            for (const auto& t : inst) key += t->toString() + ",";
            uniqueInstances[key] = inst;
        }

        for (const auto& [key, typeArgs] : uniqueInstances) {
            std::string mangled = generateMangledName(name, {}, typeArgs);
            
            // Specialize the class type
            auto specializedClass = analyzer.analyzeClassBody(classNode, typeArgs);
            specializedClass->name = mangled;

            std::map<std::string, std::shared_ptr<Type>> env;
            for (size_t i = 0; i < classNode->typeParameters.size() && i < typeArgs.size(); ++i) {
                env[classNode->typeParameters[i]->name] = typeArgs[i];
            }

            // Specialize all methods
            for (const auto& member : classNode->members) {
                if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                    if (method->isStatic) continue; // Static methods are not specialized per-instance

                    Specialization spec;
                    spec.originalName = method->name;
                    spec.specializedName = mangled + "_" + method->name;
                    spec.typeArguments = typeArgs;
                    spec.classType = specializedClass;
                    spec.node = method;
                    
                    // Infer return type for method
                    spec.returnType = analyzer.analyzeMethodBody(method, specializedClass, typeArgs);
                    
                    spec.argTypes.push_back(specializedClass);
                    for (const auto& p : method->parameters) {
                        auto pType = analyzer.parseType(p->type, analyzer.getSymbolTable());
                        spec.argTypes.push_back(analyzer.substitute(pType, env));
                    }

                    specializations.push_back(spec);
                }
            }
        }
    }

    syntheticFunctions.push_back(std::move(userMain));
}

std::string Monomorphizer::generateMangledName(const std::string& originalName, const std::vector<std::shared_ptr<Type>>& argTypes, const std::vector<std::shared_ptr<Type>>& typeArguments) {
    if (originalName == "main" && argTypes.empty()) {
        return "__ts_main";
    }
    std::string name = originalName;
    
    if (!typeArguments.empty()) {
        name += "_T";
        for (const auto& type : typeArguments) {
            name += "_";
            switch (type->kind) {
                case TypeKind::Int: name += "int"; break;
                case TypeKind::Double: name += "dbl"; break;
                case TypeKind::String: name += "str"; break;
                case TypeKind::Boolean: name += "bool"; break;
                case TypeKind::Class: name += std::static_pointer_cast<ClassType>(type)->name; break;
                default: name += "any"; break;
            }
        }
    }

    for (const auto& type : argTypes) {
        name += "_";
        switch (type->kind) {
            case TypeKind::Int: name += "int"; break;
            case TypeKind::Double: name += "dbl"; break;
            case TypeKind::String: name += "str"; break;
            case TypeKind::Boolean: name += "bool"; break;
            case TypeKind::Void: name += "void"; break;
            case TypeKind::Class: 
                name += std::static_pointer_cast<ClassType>(type)->name; 
                break;
            case TypeKind::Interface:
                name += std::static_pointer_cast<InterfaceType>(type)->name;
                break;
            case TypeKind::Array:
                name += "arr";
                if (auto arr = std::dynamic_pointer_cast<ArrayType>(type)) {
                    if (arr->elementType->kind == TypeKind::String) name += "str";
                    else if (arr->elementType->kind == TypeKind::Int) name += "int";
                    else if (arr->elementType->kind == TypeKind::Double) name += "dbl";
                }
                break;
            default: name += "any"; break;
        }
    }
    return name;
}

ast::FunctionDeclaration* Monomorphizer::findFunction(Analyzer& analyzer, const std::string& name) {
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        for (auto& stmt : module->ast->body) {
            if (auto func = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                if (func->name == name) return func;
            }
        }
    }
    return nullptr;
}

ast::ClassDeclaration* Monomorphizer::findClass(Analyzer& analyzer, const std::string& name) {
    for (auto& [path, module] : analyzer.modules) {
        if (!module->ast) continue;
        for (auto& stmt : module->ast->body) {
            if (auto cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
                if (cls->name == name) return cls;
            }
        }
    }
    return nullptr;
}

} // namespace ts
