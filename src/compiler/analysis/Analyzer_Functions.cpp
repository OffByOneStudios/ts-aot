#include "Analyzer.h"
#include <fmt/core.h>
#include <iostream>

namespace ts {

using namespace ast;

void Analyzer::visitFunctionDeclaration(FunctionDeclaration* node) {
    std::cerr << "Visiting function declaration: " << node->name << std::endl;
    symbols.enterScope();

    auto funcType = std::make_shared<FunctionType>();
    funcType->node = node;
    
    // Register type parameters first
    for (const auto& tp : node->typeParameters) {
        auto tpType = std::make_shared<TypeParameterType>(tp->name);
        if (!tp->constraint.empty()) {
            tpType->constraint = parseType(tp->constraint, symbols);
        }
        funcType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    if (!node->returnType.empty()) {
        funcType->returnType = parseType(node->returnType, symbols);
    } else {
        funcType->returnType = std::make_shared<Type>(TypeKind::Void); 
    }

    if (node->isAsync) {
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
        if (!param->type.empty()) {
            funcType->paramTypes.push_back(parseType(param->type, symbols));
        } else {
            funcType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    }

    // Define function in the current scope
    symbols.define(node->name, funcType);
    if (node->isExported && currentModule) {
        currentModule->exports->define(node->name, funcType);
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

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

void Analyzer::visitMethodDefinition(MethodDefinition* node, std::shared_ptr<ClassType> classType) {
    std::string oldMethod = currentMethodName;
    currentMethodName = node->name;

    if (node->name == "constructor" && currentClass) {
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
    
    symbols.enterScope();
    // Register type parameters
    for (const auto& tp : node->typeParameters) {
        auto tpType = std::make_shared<TypeParameterType>(tp->name);
        if (!tp->constraint.empty()) {
            tpType->constraint = parseType(tp->constraint, symbols);
        }
        methodType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    if (node->isGetter) {
        methodType->returnType = parseType(node->returnType, symbols);
    } else if (node->isSetter) {
        methodType->returnType = std::make_shared<Type>(TypeKind::Void);
        if (node->parameters.size() > 0) {
            methodType->paramTypes.push_back(parseType(node->parameters[0]->type, symbols));
        } else {
            methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }
    } else {
        if (!node->returnType.empty()) {
            methodType->returnType = parseType(node->returnType, symbols);
        } else {
            methodType->returnType = std::make_shared<Type>(TypeKind::Void);
        }
        
        for (const auto& param : node->parameters) {
            if (!param->type.empty()) {
                methodType->paramTypes.push_back(parseType(param->type, symbols));
            } else {
                methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            }
        }
    }

    // Define method in the class scope (which is the current scope before we entered a new one for type params)
    // Actually, method definitions are added to the ClassType, not just the symbol table.
    
    // Define 'this' (only for instance methods)
    if (!node->isStatic) {
        symbols.define("this", classType);
    }

    // Define parameters in scope
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        declareBindingPattern(node->parameters[i]->name.get(), methodType->paramTypes[i]);
    }

    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    symbols.exitScope();

    // Add to class
    if (classType) {
        if (node->isGetter) classType->getters[node->name] = methodType;
        else if (node->isSetter) classType->setters[node->name] = methodType;
        else if (node->name == "constructor") {
             // Handle constructor overloads if needed
        } else {
            classType->methods[node->name] = methodType;
        }
    }

    currentMethodName = oldMethod;
}

std::shared_ptr<Type> Analyzer::analyzeFunctionBody(FunctionDeclaration* node, const std::vector<std::shared_ptr<Type>>& argTypes, const std::vector<std::shared_ptr<Type>>& typeArguments) {
    symbols.enterScope();
    
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
        if (i < argTypes.size()) {
            declareBindingPattern(node->parameters[i]->name.get(), argTypes[i]);
        } else {
            // Handle default parameters or optional parameters
            declareBindingPattern(node->parameters[i]->name.get(), std::make_shared<Type>(TypeKind::Any));
        }
    }

    std::shared_ptr<Type> inferredReturnType = std::make_shared<Type>(TypeKind::Void);
    
    for (auto& stmt : node->body) {
        visit(stmt.get());
        if (stmt->getKind() == "ReturnStatement") {
            inferredReturnType = lastType;
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
    
    symbols.exitScope();
    return inferredReturnType;
}

void Analyzer::visitArrowFunction(ast::ArrowFunction* node) {
    std::cerr << "Visiting arrow function, async: " << node->isAsync << std::endl;
    auto funcType = std::make_shared<FunctionType>();
    
    symbols.enterScope();
    
    for (auto& param : node->parameters) {
        std::shared_ptr<Type> paramType = parseType(param->type, symbols);
        funcType->paramTypes.push_back(paramType);
        
        if (auto id = dynamic_cast<ast::Identifier*>(param->name.get())) {
            symbols.define(id->name, paramType);
        }
    }
    
    visit(node->body.get());
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

} // namespace ts
