#include "Analyzer.h"
#include <fmt/core.h>
#include <iostream>
#include <set>
#include <spdlog/spdlog.h>

namespace ts {

using namespace ast;

void Analyzer::visitFunctionDeclaration(ast::FunctionDeclaration* node) {
    auto funcType = std::make_shared<FunctionType>();
    funcType->node = node;

    // Check for function-level "use strict" directive
    bool functionStrictMode = globalStrictMode;
    if (!node->body.empty()) {
        if (auto exprStmt = dynamic_cast<ast::ExpressionStatement*>(node->body[0].get())) {
            if (auto strLit = dynamic_cast<ast::StringLiteral*>(exprStmt->expression.get())) {
                if (strLit->value == "use strict") {
                    functionStrictMode = true;
                }
            }
        }
    }

    // Strict mode checks for parameters
    if (functionStrictMode) {
        std::set<std::string> paramNames;
        for (const auto& param : node->parameters) {
            if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
                // Check for duplicate parameter names
                if (paramNames.count(id->name)) {
                    reportError("Strict mode: Duplicate parameter name '" + id->name + "' in function '" + node->name + "'");
                }
                paramNames.insert(id->name);

                // Check for reserved names
                if (id->name == "eval" || id->name == "arguments") {
                    reportError("Strict mode: '" + id->name + "' cannot be used as a parameter name");
                }
            }
        }

        // Check for reserved function names
        if (node->name == "eval" || node->name == "arguments") {
            reportError("Strict mode: '" + node->name + "' cannot be used as a function name");
        }
    }

    // Save/restore strict mode for this function's scope
    bool savedStrictMode = strictMode;
    strictMode = functionStrictMode;

    for (const auto& decorator : node->decorators) {
        if (decorator.name == "ts_aot.comptime") {
            funcType->isComptime = true;
            break;
        }
    }
    
    // Register type parameters first
    for (const auto& tp : node->typeParameters) {
        auto tpType = std::make_shared<TypeParameterType>(tp->name);
        if (!tp->constraint.empty()) {
            tpType->constraint = parseType(tp->constraint, symbols);
        }
        if (!tp->defaultType.empty()) {
            tpType->defaultType = parseType(tp->defaultType, symbols);
        }
        funcType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    if (!node->returnType.empty()) {
        funcType->returnType = parseType(node->returnType, symbols);
    } else {
        funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    }

    if (currentModuleType == ModuleType::UntypedJavaScript) {
        funcType->returnType = std::make_shared<Type>(TypeKind::Any);
    }

    if (node->isGenerator) {
        auto genClass = std::static_pointer_cast<ClassType>(symbols.lookupType(node->isAsync ? "AsyncGenerator" : "Generator"));
        auto wrapped = std::make_shared<ClassType>(node->isAsync ? "AsyncGenerator" : "Generator");
        wrapped->methods = genClass->methods;
        wrapped->typeArguments = { funcType->returnType };
        funcType->returnType = wrapped;
    } else if (node->isAsync) {
        // Wrap return type in Promise if it's not already a Promise
        bool isPromise = false;
        if (funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            if (cls->name == "Promise") isPromise = true;
        }
        
        if (!isPromise) {
            auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
            auto wrapped = std::make_shared<ClassType>("Promise");
            wrapped->typeParameters = promiseClass->typeParameters;
            wrapped->methods = promiseClass->methods;
            wrapped->staticMethods = promiseClass->staticMethods;
            wrapped->typeArguments = { funcType->returnType };
            funcType->returnType = wrapped;
        }
    }
    
    for (const auto& param : node->parameters) {
        std::shared_ptr<Type> pType;
        if (currentModuleType == ModuleType::UntypedJavaScript) {
            pType = std::make_shared<Type>(TypeKind::Any);
        } else if (!param->type.empty()) {
            pType = parseType(param->type, symbols);
        } else {
            pType = std::make_shared<Type>(TypeKind::Any);
        }

        if (param->isRest && pType->kind != TypeKind::Array) {
            pType = std::make_shared<ArrayType>(pType);
        }

        funcType->paramTypes.push_back(pType);
        funcType->isOptional.push_back(param->isOptional);
        if (param->isRest) funcType->hasRest = true;
    }

    // Define function in the current scope (if not already hoisted)
    auto existing = symbols.lookup(node->name);
    if (!existing || existing->type->kind != TypeKind::Function) {
        symbols.define(node->name, funcType);
        if (node->isExported && currentModule) {
            currentModule->exports->define(node->name, funcType);
        }
        if (node->isDefaultExport && currentModule) {
            currentModule->exports->define("default", funcType);
        }
    } else {
        // Update the hoisted type with the real one (or just use the hoisted one)
        auto hoisted = std::static_pointer_cast<FunctionType>(existing->type);
        hoisted->returnType = funcType->returnType;
        hoisted->paramTypes = funcType->paramTypes;
        hoisted->typeParameters = funcType->typeParameters;
        hoisted->hasRest = funcType->hasRest;
        hoisted->isOptional = funcType->isOptional;
        funcType = hoisted;
    }

    symbols.enterScope();

    // Re-define type parameters in the new scope for the body
    for (const auto& tpType : funcType->typeParameters) {
        symbols.defineType(tpType->name, tpType);
    }

    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        declareBindingPattern(node->parameters[i]->name.get(), funcType->paramTypes[i]);
    }

    // Hoist variable declarations in this function scope (JS/TS 'var' semantics).
    // This prevents "undefined variable" errors when a nested function references
    // a variable declared later in the same function.
    for (auto& stmt : node->body) {
        if (auto var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            if (auto id = dynamic_cast<ast::Identifier*>(var->name.get())) {
                if (!symbols.lookup(id->name)) {
                    symbols.define(id->name, std::make_shared<Type>(TypeKind::Any));
                }
            } else {
                // Destructuring patterns: conservatively declare members as Any
                declareBindingPattern(var->name.get(), std::make_shared<Type>(TypeKind::Any));
            }
        } else if (auto fn = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            // Nested function declarations are hoisted.
            if (!symbols.lookup(fn->name)) {
                auto nestedType = std::make_shared<FunctionType>();
                nestedType->node = fn;
                symbols.define(fn->name, nestedType);
            }
        }
    }

    functionDepth++;
    std::shared_ptr<Type> oldReturnType = currentReturnType;
    currentReturnType = nullptr;

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }

    if (node->returnType.empty() && currentReturnType) {
        // Update function return type based on inferred return type
        funcType->returnType = currentReturnType;
        
        // Re-wrap in Promise/Generator if needed
        if (node->isGenerator) {
            auto genClass = std::static_pointer_cast<ClassType>(symbols.lookupType(node->isAsync ? "AsyncGenerator" : "Generator"));
            auto wrapped = std::make_shared<ClassType>(node->isAsync ? "AsyncGenerator" : "Generator");
            wrapped->methods = genClass->methods;
            wrapped->typeArguments = { funcType->returnType };
            funcType->returnType = wrapped;
        } else if (node->isAsync) {
            auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
            auto wrapped = std::make_shared<ClassType>("Promise");
            wrapped->methods = promiseClass->methods;
            wrapped->typeArguments = { funcType->returnType };
            funcType->returnType = wrapped;
        }
    }

    currentReturnType = oldReturnType;
    functionDepth--;
    symbols.exitScope();

    // Restore strict mode
    strictMode = savedStrictMode;
}


void Analyzer::visitMethodDefinition(ast::MethodDefinition* node) {
    visitMethodDefinition(node, nullptr);
}

void Analyzer::visitMethodDefinition(MethodDefinition* node, std::shared_ptr<ClassType> classType) {
    if (node->nameNode) {
        if (dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
            visit(node->nameNode.get());
        }
    }
    std::string oldMethod = currentMethodName;
    currentMethodName = node->name;

    if (node->name == "constructor" && classType) {
        std::vector<StmtPtr> injected;
        for (const auto& param : node->parameters) {
            if (param->isParameterProperty) {
                auto id = dynamic_cast<Identifier*>(param->name.get());
                if (!id) continue;

                auto thisExpr = std::make_unique<Identifier>();
                thisExpr->name = "this";
                auto propAccess = std::make_unique<PropertyAccessExpression>();
                propAccess->expression = std::move(thisExpr);
                propAccess->name = id->name;
                
                auto rhs = std::make_unique<Identifier>();
                rhs->name = id->name;
                
                auto assign = std::make_unique<AssignmentExpression>();
                assign->left = std::move(propAccess);
                assign->right = std::move(rhs);
                
                auto stmt = std::make_unique<ExpressionStatement>();
                stmt->expression = std::move(assign);
                injected.push_back(std::move(stmt));
            }
        }
        if (!injected.empty()) {
            node->body.insert(node->body.begin(), 
                std::make_move_iterator(injected.begin()), 
                std::make_move_iterator(injected.end()));
        }
    }

    auto methodType = std::make_shared<FunctionType>();
    bool needsReturnTypeInference = false;  // Track if we need to infer return type
    
    symbols.enterScope();
    // Register type parameters
    for (const auto& tp : node->typeParameters) {
        auto tpType = std::make_shared<TypeParameterType>(tp->name);
        if (!tp->constraint.empty()) {
            tpType->constraint = parseType(tp->constraint, symbols);
        }
        if (!tp->defaultType.empty()) {
            tpType->defaultType = parseType(tp->defaultType, symbols);
        }
        methodType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    if (node->isGetter) {
        if (!node->returnType.empty()) {
            // Handle 'this' return type (though uncommon for getters)
            if (node->returnType == "this" && classType) {
                methodType->returnType = classType;
            } else {
                methodType->returnType = parseType(node->returnType, symbols);
            }
        } else {
            // Getters infer their return type from the body
            methodType->returnType = std::make_shared<Type>(TypeKind::Void);
            needsReturnTypeInference = true;
        }
    } else if (node->isSetter) {
        methodType->returnType = std::make_shared<Type>(TypeKind::Void);
        if (node->parameters.size() > 0) {
            methodType->paramTypes.push_back(parseType(node->parameters[0]->type, symbols));
        } else {
            methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    } else {
        if (!node->returnType.empty()) {
            // Handle 'this' return type for method chaining
            if (node->returnType == "this" && classType) {
                methodType->returnType = classType;
            } else {
                methodType->returnType = parseType(node->returnType, symbols);
            }
        } else {
            // Start with void, will be updated by return type inference
            methodType->returnType = std::make_shared<Type>(TypeKind::Void);
        }

        needsReturnTypeInference = node->returnType.empty();  // Set the outer variable, not a new local
        if (currentModuleType == ModuleType::UntypedJavaScript) {
            methodType->returnType = std::make_shared<Type>(TypeKind::Any);
            needsReturnTypeInference = false;
        }

        if (node->isGenerator) {
            auto genClass = std::static_pointer_cast<ClassType>(symbols.lookupType(node->isAsync ? "AsyncGenerator" : "Generator"));
            auto wrapped = std::make_shared<ClassType>(node->isAsync ? "AsyncGenerator" : "Generator");
            wrapped->methods = genClass->methods;
            wrapped->typeArguments = { methodType->returnType };
            methodType->returnType = wrapped;
        } else if (node->isAsync) {
            // Wrap return type in Promise if it's not already a Promise
            bool isPromise = false;
            if (methodType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(methodType->returnType);
                if (cls->name == "Promise") isPromise = true;
            }
            
            if (!isPromise) {
                auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
                auto wrapped = std::make_shared<ClassType>("Promise");
                wrapped->typeParameters = promiseClass->typeParameters;
                wrapped->methods = promiseClass->methods;
                wrapped->staticMethods = promiseClass->staticMethods;
                wrapped->typeArguments = { methodType->returnType };
                methodType->returnType = wrapped;
            }
        }
        
        for (const auto& param : node->parameters) {
            if (currentModuleType == ModuleType::UntypedJavaScript) {
                methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            } else if (!param->type.empty()) {
                methodType->paramTypes.push_back(parseType(param->type, symbols));
            } else {
                methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
            methodType->isOptional.push_back(param->isOptional);
            if (param->isRest) methodType->hasRest = true;
        }
    }

    // Define method in the class scope (which is the current scope before we entered a new one for type params)
    // Actually, method definitions are added to the ClassType, not just the symbol table.
    
    // Define 'this'
    if (node->isStatic) {
        // In static methods, 'this' is the constructor function (which has the class type as its return type)
        auto ctorType = std::make_shared<FunctionType>();
        ctorType->returnType = classType;
        symbols.define("this", ctorType);
    } else if (classType) {
        symbols.define("this", classType);
    } else {
        // Object literal method - 'this' is the object itself (typed as Any for now)
        symbols.define("this", std::make_shared<Type>(TypeKind::Any));
    }

    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        declareBindingPattern(node->parameters[i]->name.get(), methodType->paramTypes[i]);
    }

    // Track the inferred return type from return statements
    std::shared_ptr<Type> inferredReturnType = std::make_shared<Type>(TypeKind::Void);

    functionDepth++;
    for (auto& stmt : node->body) {
        visit(stmt.get());
        // Infer return type from return statements
        if (needsReturnTypeInference && stmt->getKind() == "ReturnStatement") {
            if (lastType) {
                inferredReturnType = lastType;
            }
        }
    }
    functionDepth--;

    // Update method return type if inferred
    if (needsReturnTypeInference && inferredReturnType->kind != TypeKind::Void) {
        methodType->returnType = inferredReturnType;
    }

    symbols.exitScope();

    // Add to class
    if (classType) {
        std::string name = manglePrivateName(node->name, classType->name);
        if (node->isGetter) classType->getters[name] = methodType;
        else if (node->isSetter) classType->setters[name] = methodType;
        else if (node->name == "constructor") {
             // Handle constructor overloads if needed
        } else if (node->isStatic) {
            classType->staticMethods[name] = methodType;
        } else {
            classType->methods[name] = methodType;
        }
    }

    // Set the inferred type for object literal methods
    node->inferredType = methodType;
    lastType = methodType;

    currentMethodName = oldMethod;
}

// Helper function to collect return types from statements recursively
static void collectReturnTypes(ast::Node* stmt, std::vector<std::shared_ptr<Type>>& returnTypes) {
    if (!stmt) return;

    if (auto retStmt = dynamic_cast<ast::ReturnStatement*>(stmt)) {
        if (retStmt->expression && retStmt->expression->inferredType) {
            returnTypes.push_back(retStmt->expression->inferredType);
        }
        return;
    }

    if (auto ifStmt = dynamic_cast<ast::IfStatement*>(stmt)) {
        collectReturnTypes(ifStmt->thenStatement.get(), returnTypes);
        collectReturnTypes(ifStmt->elseStatement.get(), returnTypes);
        return;
    }

    if (auto block = dynamic_cast<ast::BlockStatement*>(stmt)) {
        for (auto& s : block->statements) {
            collectReturnTypes(s.get(), returnTypes);
        }
        return;
    }

    if (auto forStmt = dynamic_cast<ast::ForStatement*>(stmt)) {
        collectReturnTypes(forStmt->body.get(), returnTypes);
        return;
    }

    if (auto forOfStmt = dynamic_cast<ast::ForOfStatement*>(stmt)) {
        collectReturnTypes(forOfStmt->body.get(), returnTypes);
        return;
    }

    if (auto forInStmt = dynamic_cast<ast::ForInStatement*>(stmt)) {
        collectReturnTypes(forInStmt->body.get(), returnTypes);
        return;
    }

    if (auto whileStmt = dynamic_cast<ast::WhileStatement*>(stmt)) {
        collectReturnTypes(whileStmt->body.get(), returnTypes);
        return;
    }

    if (auto switchStmt = dynamic_cast<ast::SwitchStatement*>(stmt)) {
        for (auto& clause : switchStmt->clauses) {
            if (auto caseClause = dynamic_cast<ast::CaseClause*>(clause.get())) {
                for (auto& s : caseClause->statements) {
                    collectReturnTypes(s.get(), returnTypes);
                }
            } else if (auto defaultClause = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                for (auto& s : defaultClause->statements) {
                    collectReturnTypes(s.get(), returnTypes);
                }
            }
        }
        return;
    }

    if (auto tryStmt = dynamic_cast<ast::TryStatement*>(stmt)) {
        for (auto& s : tryStmt->tryBlock) {
            collectReturnTypes(s.get(), returnTypes);
        }
        if (tryStmt->catchClause) {
            for (auto& s : tryStmt->catchClause->block) {
                collectReturnTypes(s.get(), returnTypes);
            }
        }
        for (auto& s : tryStmt->finallyBlock) {
            collectReturnTypes(s.get(), returnTypes);
        }
        return;
    }
}

std::shared_ptr<Type> Analyzer::analyzeFunctionBody(FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes, const std::vector<std::shared_ptr<Type>>& typeArguments) {
    // If this function belongs to a different module (e.g. external JS), switch analyzer context
    // so semantic rules (permissive/untyped) match the function's origin.
    auto oldModule = currentModule;
    auto oldPath = currentFilePath;
    auto oldModuleType = currentModuleType;
    bool oldSuppressErrors = suppressErrors;
    bool oldSkipUntyped = skipUntypedSemantic;

    auto setContextForFunction = [&]() {
        // First, check explicit ownership for synthetic functions (e.g., module init).
        if (!node->name.empty()) {
            auto ownerIt = syntheticFunctionOwners.find(node->name);
            if (ownerIt != syntheticFunctionOwners.end()) {
                auto modIt = modules.find(ownerIt->second);
                if (modIt != modules.end() && modIt->second) {
                    auto module = modIt->second;
                    SPDLOG_INFO("analyzeFunctionBody: applying synthetic owner context for {} -> {} (moduleType={})",
                        node->name, module->path, static_cast<int>(module->type));
                    currentModule = module;
                    currentFilePath = module->path;
                    currentModuleType = module->type;
                    if (module->type != ModuleType::TypeScript) {
                        suppressErrors = true;
                    }
                    if (module->type == ModuleType::UntypedJavaScript) {
                        // Avoid the ultra-minimal skipUntypedSemantic behavior for bodies here.
                        skipUntypedSemantic = false;
                    }
                    return;
                }
            }
        }

        for (auto& [path, module] : modules) {
            if (!module || !module->ast) continue;
            for (auto& stmt : module->ast->body) {
                if (auto fn = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    // Prefer pointer identity, but allow name-based match for cloned/specialized nodes.
                    if (fn == node || (!node->name.empty() && fn->name == node->name)) {
                        currentModule = module;
                        currentFilePath = module->path;
                        currentModuleType = module->type;
                        if (module->type != ModuleType::TypeScript) {
                            suppressErrors = true;
                        }
                        if (module->type == ModuleType::UntypedJavaScript) {
                            // Avoid the ultra-minimal skipUntypedSemantic behavior for bodies here.
                            skipUntypedSemantic = false;
                        }
                        return;
                    }
                }
            }
        }
    };

    setContextForFunction();

    // For untyped/opaque modules, we still need to visit statements to set inferredType on AST nodes.
    // This is required for codegen to work correctly (e.g., knowing when to use ts_object_set_prop).
    // We just skip error checking and return `any`.
    bool isUntypedModule = (currentModuleType == ModuleType::UntypedJavaScript || suppressErrors || skipUntypedSemantic);

    symbols.enterScope();

    // Hoist declarations in the function body (var/function hoisting semantics).
    for (auto& stmt : node->body) {
        if (auto var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            if (auto id = dynamic_cast<ast::Identifier*>(var->name.get())) {
                if (!symbols.lookup(id->name)) {
                    symbols.define(id->name, std::make_shared<Type>(TypeKind::Any));
                }
            } else {
                declareBindingPattern(var->name.get(), std::make_shared<Type>(TypeKind::Any));
            }
        } else if (auto fn = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            if (!symbols.lookup(fn->name)) {
                auto nestedType = std::make_shared<FunctionType>();
                nestedType->node = fn;
                symbols.define(fn->name, nestedType);
            }
        }
    }
    
    // Define type parameters with actual type arguments
    for (size_t i = 0; i < node->typeParameters.size(); ++i) {
        if (i < typeArguments.size()) {
            symbols.defineType(node->typeParameters[i]->name, typeArguments[i]);
        } else {
            // If no type argument provided, use Any (or we could do inference here later)
            symbols.defineType(node->typeParameters[i]->name, std::make_shared<Type>(TypeKind::Any));
        }
    }
    
    // Define parameters with actual argument types
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        const auto& param = node->parameters[i];
        if (param->isRest) {
            // Collect all remaining argTypes into an array
            std::vector<std::shared_ptr<Type>> restTypes;
            for (size_t j = i; j < argTypes.size(); ++j) {
                restTypes.push_back(argTypes[j]);
            }
            std::shared_ptr<Type> restType;
            if (restTypes.empty()) {
                restType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
            } else if (restTypes.size() == 1) {
                // If the single arg is already an array (e.g., from spread: sumAll(...arr)),
                // use it directly instead of wrapping in another array
                if (restTypes[0]->kind == TypeKind::Array) {
                    restType = restTypes[0];  // Use array directly
                } else {
                    restType = std::make_shared<ArrayType>(restTypes[0]);
                }
            } else {
                // Check if all rest types are the same (homogeneous)
                bool homogeneous = true;
                std::string firstTypeStr = restTypes[0]->toString();
                for (size_t k = 1; k < restTypes.size(); ++k) {
                    if (restTypes[k]->toString() != firstTypeStr) {
                        homogeneous = false;
                        break;
                    }
                }
                
                if (homogeneous) {
                    // All same type -> ArrayType of that element type
                    restType = std::make_shared<ArrayType>(restTypes[0]);
                } else {
                    // Mixed types -> ArrayType of union
                    restType = std::make_shared<ArrayType>(std::make_shared<UnionType>(restTypes));
                }
            }
            SPDLOG_INFO("Inferred rest parameter type: {}", restType->toString());
            declareBindingPattern(param->name.get(), restType);
            break; // Rest parameter must be last
        } else if (i < argTypes.size()) {
            std::shared_ptr<Type> paramType = argTypes[i];
            
            // If argument is undefined and there is a default value, use the default value's type
            if (param->initializer && (paramType->kind == TypeKind::Undefined || paramType->kind == TypeKind::Void)) {
                visit(param->initializer.get());
                paramType = lastType;
                SPDLOG_INFO("analyzeFunctionBody: argument is undefined, using default value type: {}", paramType->toString());
            }

            SPDLOG_INFO("analyzeFunctionBody: defining param {} with type {}", 
                dynamic_cast<ast::Identifier*>(param->name.get()) ? 
                    dynamic_cast<ast::Identifier*>(param->name.get())->name : "?",
                paramType->toString());
            declareBindingPattern(param->name.get(), paramType);
        } else {
            // Handle default parameters or optional parameters
            if (param->initializer) {
                visit(param->initializer.get());
                declareBindingPattern(param->name.get(), lastType);
            } else if (param->isOptional) {
                auto pType = parseType(param->type, symbols);
                declareBindingPattern(param->name.get(), pType);
            } else {
                declareBindingPattern(param->name.get(), std::make_shared<Type>(TypeKind::Any));
            }
        }
    }

    std::shared_ptr<Type> inferredReturnType = std::make_shared<Type>(TypeKind::Void);

    // First, check if there's an explicit return type annotation
    if (!node->returnType.empty() && node->returnType != "void") {
        inferredReturnType = parseType(node->returnType, symbols);
    }

    functionDepth++;
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    functionDepth--;

    // If no explicit return type, collect return types from all branches
    if (node->returnType.empty()) {
        std::vector<std::shared_ptr<Type>> returnTypes;
        for (auto& stmt : node->body) {
            collectReturnTypes(stmt.get(), returnTypes);
        }

        // Use the first return type found (they should all be the same in valid TypeScript)
        for (const auto& rt : returnTypes) {
            if (rt && rt->kind != TypeKind::Void && rt->kind != TypeKind::Undefined) {
                inferredReturnType = rt;
                break;
            }
        }
    }

    if (node->isAsync) {
        bool isPromise = false;
        if (inferredReturnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(inferredReturnType);
            if (cls->name == "Promise") isPromise = true;
        }
        
        if (!isPromise) {
            auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
            
            std::string mangledName = "Promise_" + inferredReturnType->toString();
            std::replace_if(mangledName.begin(), mangledName.end(), [](char c) {
                return !std::isalnum(c);
            }, '_');

            auto wrapped = std::make_shared<ClassType>(mangledName);
            wrapped->methods = promiseClass->methods;
            wrapped->staticMethods = promiseClass->staticMethods;
            wrapped->typeArguments = { inferredReturnType };
            inferredReturnType = wrapped;

            if (!symbols.lookupType(mangledName)) {
                symbols.defineGlobalType(mangledName, wrapped);
            }
        }
    }

    if (node->isGenerator) {
        auto genClass = std::static_pointer_cast<ClassType>(symbols.lookupType(node->isAsync ? "AsyncGenerator" : "Generator"));
        auto wrapped = std::make_shared<ClassType>(node->isAsync ? "AsyncGenerator" : "Generator");
        wrapped->methods = genClass->methods;
        wrapped->typeArguments = { inferredReturnType };
        inferredReturnType = wrapped;
    }
    
    symbols.exitScope();

    currentModule = oldModule;
    currentFilePath = oldPath;
    currentModuleType = oldModuleType;
    suppressErrors = oldSuppressErrors;
    skipUntypedSemantic = oldSkipUntyped;
    
    // For untyped modules, always return any regardless of inferred type
    if (isUntypedModule) {
        return std::make_shared<Type>(TypeKind::Any);
    }
    return inferredReturnType;
}

void Analyzer::visitArrowFunction(ast::ArrowFunction* node) {
    auto funcType = std::make_shared<FunctionType>();
    
    // Check for contextual type (expected callback signature from caller)
    auto contextualType = getContextualType();
    std::shared_ptr<FunctionType> contextualFuncType = nullptr;
    if (contextualType && contextualType->kind == TypeKind::Function) {
        contextualFuncType = std::static_pointer_cast<FunctionType>(contextualType);
    }
    
    symbols.enterScope();
    
    for (size_t i = 0; i < node->parameters.size(); i++) {
        auto& param = node->parameters[i];
        std::shared_ptr<Type> paramType = parseType(param->type, symbols);
        
        if (currentModuleType == ModuleType::UntypedJavaScript) {
            paramType = std::make_shared<Type>(TypeKind::Any);
        }
        
        // If param type is Any and we have a contextual type, use that instead
        if (paramType->kind == TypeKind::Any && contextualFuncType && i < contextualFuncType->paramTypes.size()) {
            paramType = contextualFuncType->paramTypes[i];
            SPDLOG_INFO("Contextual typing: parameter {} gets type {} from callback signature", 
                i, paramType->toString());
        }
        
        funcType->paramTypes.push_back(paramType);
        
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            symbols.define(id->name, paramType);
        }
    }
    
    functionDepth++;
    visit(node->body.get());
    functionDepth--;
    funcType->returnType = lastType;

    if (node->isAsync) {
        // Wrap return type in Promise if it's not already a Promise
        bool isPromise = false;
        if (funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            if (cls->name == "Promise" || cls->name.substr(0, 8) == "Promise_") isPromise = true;
        }
        
        if (!isPromise) {
            auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
            
            std::string mangledName = "Promise_" + funcType->returnType->toString();
            std::replace_if(mangledName.begin(), mangledName.end(), [](char c) {
                return !std::isalnum(c);
            }, '_');

            auto wrapped = std::make_shared<ClassType>(mangledName);
            wrapped->methods = promiseClass->methods;
            wrapped->staticMethods = promiseClass->staticMethods;
            wrapped->typeArguments = { funcType->returnType };
            funcType->returnType = wrapped;

            if (!symbols.lookupType(mangledName)) {
                symbols.defineGlobalType(mangledName, wrapped);
            }
        }
    }
    
    symbols.exitScope();
    
    node->inferredType = funcType;
    lastType = funcType;
}

void Analyzer::visitFunctionExpression(ast::FunctionExpression* node) {
    auto funcType = std::make_shared<FunctionType>();
    funcType->node = node;

    symbols.enterScope();

    // Register type parameters
    for (const auto& tp : node->typeParameters) {
        auto tpType = std::make_shared<TypeParameterType>(tp->name);
        if (!tp->constraint.empty()) {
            tpType->constraint = parseType(tp->constraint, symbols);
        }
        if (!tp->defaultType.empty()) {
            tpType->defaultType = parseType(tp->defaultType, symbols);
        }
        funcType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    // Return type will be inferred after visiting body if not explicitly specified
    bool needsReturnTypeInference = node->returnType.empty();
    if (!needsReturnTypeInference) {
        funcType->returnType = parseType(node->returnType, symbols);
    } else {
        // Start with void, will be updated by return statements
        funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    }

    if (currentModuleType == ModuleType::UntypedJavaScript) {
        funcType->returnType = std::make_shared<Type>(TypeKind::Any);
        needsReturnTypeInference = false;
    }

    if (node->isGenerator) {
        auto genClass = std::static_pointer_cast<ClassType>(symbols.lookupType(node->isAsync ? "AsyncGenerator" : "Generator"));
        auto wrapped = std::make_shared<ClassType>(node->isAsync ? "AsyncGenerator" : "Generator");
        wrapped->methods = genClass->methods;
        wrapped->typeArguments = { funcType->returnType };
        funcType->returnType = wrapped;
    } else if (node->isAsync) {
        // Wrap return type in Promise if it's not already a Promise
        bool isPromise = false;
        if (funcType->returnType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
            if (cls->name == "Promise" || cls->name.substr(0, 8) == "Promise_") isPromise = true;
        }
        
        if (!isPromise) {
            auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
            
            std::string mangledName = "Promise_" + funcType->returnType->toString();
            std::replace_if(mangledName.begin(), mangledName.end(), [](char c) {
                return !std::isalnum(c);
            }, '_');

            auto wrapped = std::make_shared<ClassType>(mangledName);
            wrapped->methods = promiseClass->methods;
            wrapped->staticMethods = promiseClass->staticMethods;
            wrapped->typeArguments = { funcType->returnType };
            funcType->returnType = wrapped;

            if (!symbols.lookupType(mangledName)) {
                symbols.defineGlobalType(mangledName, wrapped);
            }
        }
    }
    
    for (const auto& param : node->parameters) {
        std::shared_ptr<Type> pType;
        if (currentModuleType == ModuleType::UntypedJavaScript) {
            pType = std::make_shared<Type>(TypeKind::Any);
        } else if (!param->type.empty()) {
            pType = parseType(param->type, symbols);
        } else {
            pType = std::make_shared<Type>(TypeKind::Any);
        }
        funcType->paramTypes.push_back(pType);
        funcType->isOptional.push_back(param->isOptional);
        if (param->isRest) funcType->hasRest = true;
    }

    // If the function expression has a name, it's visible inside the function
    if (!node->name.empty()) {
        symbols.define(node->name, funcType);
    }

    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        declareBindingPattern(node->parameters[i]->name.get(), funcType->paramTypes[i]);
    }

    // Track the inferred return type from return statements
    std::shared_ptr<Type> inferredReturnType = std::make_shared<Type>(TypeKind::Void);

    functionDepth++;
    for (auto& stmt : node->body) {
        visit(stmt.get());
        // Infer return type from return statements
        if (needsReturnTypeInference && stmt->getKind() == "ReturnStatement") {
            if (lastType) {
                inferredReturnType = lastType;
            }
        }
    }
    functionDepth--;

    // Update function return type if inferred
    if (needsReturnTypeInference && inferredReturnType->kind != TypeKind::Void) {
        funcType->returnType = inferredReturnType;
        
        // Handle async wrapping for inferred return type
        if (node->isAsync) {
            bool isPromise = false;
            if (funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
                if (cls->name == "Promise" || cls->name.substr(0, 8) == "Promise_") isPromise = true;
            }
            
            if (!isPromise) {
                auto promiseClass = std::static_pointer_cast<ClassType>(symbols.lookupType("Promise"));
                
                std::string mangledName = "Promise_" + funcType->returnType->toString();
                std::replace_if(mangledName.begin(), mangledName.end(), [](char c) {
                    return !std::isalnum(c);
                }, '_');

                auto wrapped = std::make_shared<ClassType>(mangledName);
                wrapped->methods = promiseClass->methods;
                wrapped->staticMethods = promiseClass->staticMethods;
                wrapped->typeArguments = { funcType->returnType };
                funcType->returnType = wrapped;

                if (!symbols.lookupType(mangledName)) {
                    symbols.defineGlobalType(mangledName, wrapped);
                }
            }
        }
    }
    
    symbols.exitScope();
    
    node->inferredType = funcType;
    lastType = funcType;
}

} // namespace ts

