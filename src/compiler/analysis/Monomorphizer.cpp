#include "Monomorphizer.h"
#include "Analyzer.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <set>
#include <iostream>

namespace ts {

Monomorphizer::Monomorphizer() {}

void Monomorphizer::monomorphize(ast::Program* program, Analyzer& analyzer) {
    const auto& usages = analyzer.getFunctionUsages();
    std::vector<std::string> moduleInitFunctions;

    for (const auto& path : analyzer.moduleOrder) {
        // fmt::print("Processing module: {}\n", path);
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
        if (module->isAsync) {
            SPDLOG_DEBUG("Module {} is async, marking init as async", path);
        }
        moduleInitFunctions.push_back(initName);

        std::vector<std::unique_ptr<ast::Statement>> newBody;
        for (auto& stmt : module->ast->body) {
            std::string kind = stmt->getKind();
            if (kind == "FunctionDeclaration" || 
                kind == "ClassDeclaration" ||
                kind == "InterfaceDeclaration" ||
                kind == "ImportDeclaration" ||
                kind == "ExportDeclaration") {
                newBody.push_back(std::move(stmt));
            } else {
                // Move everything else (VariableDeclarations, ExpressionStatements, etc.) to module init
                // fmt::print("Moving {} to module init\n", kind);
                moduleInit->body.push_back(std::move(stmt));
            }
        }

        // If the last statement is an expression, turn it into a return
        if (!moduleInit->body.empty()) {
            if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(moduleInit->body.back().get())) {
                auto retStmt = std::make_unique<ast::ReturnStatement>();
                retStmt->expression = std::move(exprStmt->expression);
                moduleInit->body.back() = std::move(retStmt);
            }
        }

        // fmt::print("Module init now has {} statements\n", moduleInit->body.size());
        module->ast->body = std::move(newBody);

        Specialization spec;
        spec.originalName = initName;
        spec.specializedName = initName;
        spec.argTypes = {};
        spec.returnType = std::make_shared<Type>(TypeKind::Any);
        spec.node = moduleInit.get();
        
        // Set line number to 1 for synthetic module init
        moduleInit->line = 1;
        moduleInit->column = 1;
        
        auto ft = std::make_shared<FunctionType>();
        ft->returnType = std::make_shared<Type>(TypeKind::Any);
        analyzer.getSymbolTable().define(initName, ft);

        specializations.push_back(spec);
        syntheticFunctions.push_back(std::move(moduleInit));
    }

    // Special case for the program passed to monomorphize if it's not in analyzer.modules
    // (Though it should be now)
    
    // Create user_main that calls all module inits
    auto userMain = std::make_unique<ast::FunctionDeclaration>();
    userMain->name = "user_main";
    
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

    for (size_t i = 0; i < moduleInitFunctions.size(); ++i) {
        const auto& initName = moduleInitFunctions[i];
        const auto& path = analyzer.moduleOrder[i];
        auto call = std::make_unique<ast::CallExpression>();
        auto callId = std::make_unique<ast::Identifier>();
        callId->name = initName;
        call->callee = std::move(callId);
        
        auto it = analyzer.modules.find(path);
        if (it != analyzer.modules.end() && it->second->isAsync) {
             auto promiseClass = std::static_pointer_cast<ClassType>(analyzer.getSymbolTable().lookupType("Promise"));
             auto wrapped = std::make_shared<ClassType>("Promise");
             wrapped->methods = promiseClass->methods;
             wrapped->typeArguments = { std::make_shared<Type>(TypeKind::Void) };
             call->inferredType = wrapped;
        } else {
             call->inferredType = std::make_shared<Type>(TypeKind::Void);
        }

        std::unique_ptr<ast::Expression> finalExpr;
        if (anyAsync) {
            auto awaitExpr = std::make_unique<ast::AwaitExpression>();
            awaitExpr->expression = std::move(call);
            awaitExpr->inferredType = std::make_shared<Type>(TypeKind::Void);
            finalExpr = std::move(awaitExpr);
        } else {
            finalExpr = std::move(call);
        }

        if (i == moduleInitFunctions.size() - 1) {
            auto stmt = std::make_unique<ast::ReturnStatement>();
            stmt->expression = std::move(finalExpr);
            userMain->body.push_back(std::move(stmt));
        } else {
            auto stmt = std::make_unique<ast::ExpressionStatement>();
            stmt->expression = std::move(finalExpr);
            userMain->body.push_back(std::move(stmt));
        }
    }

    Specialization mainSpec;
    mainSpec.originalName = "user_main";
    mainSpec.specializedName = "user_main";
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
                SPDLOG_INFO("Monomorphizer created spec for {} with node {}", name, fmt::ptr(funcNode));
                
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
