#include "Analyzer.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>

namespace ts {

using namespace ast;

void Analyzer::visitClassDeclaration(ast::ClassDeclaration* node) {
    auto classType = std::make_shared<ClassType>(node->name);
    classType->originalName = node->name;
    classType->isAbstract = node->isAbstract;
    classType->isStruct = node->isStruct;
    classType->node = node;
    
    auto existing = symbols.lookupType(node->name);
    if (!existing || existing->kind != TypeKind::Class) {
        symbols.defineType(node->name, classType);
        if (node->isExported && currentModule) {
            currentModule->exports->defineType(node->name, classType);
        }
        if (node->isDefaultExport && currentModule) {
            currentModule->exports->defineType("default", classType);
        }
    } else {
        classType = std::static_pointer_cast<ClassType>(existing);
        classType->isStruct = node->isStruct;
        classType->isAbstract = node->isAbstract;
    }

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
        classType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    // 1. Resolve base class
    if (!node->baseClass.empty()) {
        auto base = symbols.lookupType(node->baseClass);
        if (!base || base->kind != TypeKind::Class) {
            reportError(fmt::format("Base class {} not found or not a class", node->baseClass));
        } else {
            classType->baseClass = std::static_pointer_cast<ClassType>(base);
            // Inherit abstract methods
            classType->abstractMethods = classType->baseClass->abstractMethods;
        }
    }

    // 2.5 Resolve implemented interfaces
    for (const auto& interfaceName : node->implementsInterfaces) {
        auto interfaceType = symbols.lookupType(interfaceName);
        if (!interfaceType || interfaceType->kind != TypeKind::Interface) {
            reportError(fmt::format("Interface {} not found or not an interface", interfaceName));
        } else {
            classType->implementsInterfaces.push_back(std::static_pointer_cast<InterfaceType>(interfaceType));
        }
    }

    // 3. Scan members to populate fields and methods
    for (const auto& member : node->members) {
        bool isComptime = false;
        for (const auto& decorator : member->decorators) {
            if (decorator == "ts_aot.comptime") {
                isComptime = true;
                break;
            }
        }

        if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            auto propType = parseType(prop->type, symbols);
            std::string name = manglePrivateName(prop->name, node->name);
            if (prop->isStatic) {
                auto existingType = classType->staticFields.count(name) ? classType->staticFields[name] : nullptr;
                bool isNewTypeBetter = (propType->kind != TypeKind::Any && propType->kind != TypeKind::Unknown);
                bool isOldTypeGeneric = (!existingType || existingType->kind == TypeKind::Any || existingType->kind == TypeKind::Unknown);

                if (isOldTypeGeneric || isNewTypeBetter) {
                    SPDLOG_DEBUG("  Updating static field {} to type: {}", name, propType->toString());
                    classType->staticFields[name] = propType;
                }
                classType->staticFieldAccess[name] = prop->access;
                if (prop->isReadonly) {
                    classType->staticReadonlyFields.insert(name);
                }
                if (isComptime) {
                    classType->staticComptimeFields.insert(name);
                }
            } else {
                auto existingType = classType->fields.count(name) ? classType->fields[name] : nullptr;
                bool isNewTypeBetter = (propType->kind != TypeKind::Any && propType->kind != TypeKind::Unknown);
                bool isOldTypeGeneric = (!existingType || existingType->kind == TypeKind::Any || existingType->kind == TypeKind::Unknown);

                if (isOldTypeGeneric || isNewTypeBetter) {
                    classType->fields[name] = propType;
                }
                classType->fieldAccess[name] = prop->access;
                if (prop->isReadonly) {
                    classType->readonlyFields.insert(name);
                }
                if (isComptime) {
                    classType->comptimeFields.insert(name);
                }
            }
        }
        if (auto indexSig = dynamic_cast<IndexSignature*>(member.get())) {
            // Handle index signature: [key: string]: valueType
            auto valueType = parseType(indexSig->valueType, symbols);
            classType->indexSignatureValueType = valueType;
            classType->indexSignatureReadonly = indexSig->isReadonly;
            SPDLOG_DEBUG("  Class {} has index signature with value type: {}", node->name, valueType->toString());
        }
        if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            std::string name = manglePrivateName(method->name, node->name);
            if (isComptime) {
                if (method->isStatic) {
                    classType->staticComptimeMethods.insert(name);
                } else {
                    classType->comptimeMethods.insert(name);
                }
            }
            if (method->name == "constructor") {
                for (const auto& param : method->parameters) {
                    if (param->isParameterProperty) {
                        auto id = dynamic_cast<Identifier*>(param->name.get());
                        if (id) {
                            auto fieldType = parseType(param->type, symbols);
                            std::string name = manglePrivateName(id->name, node->name);
                            classType->fields[name] = fieldType;
                            classType->fieldAccess[name] = param->access;
                            if (param->isReadonly) {
                                classType->readonlyFields.insert(name);
                            }
                        }
                    }
                }
            }
            if (method->isAbstract) {
                if (!node->isAbstract) {
                    reportError(fmt::format("Abstract method '{}' can only be declared in an abstract class.", method->name));
                }
                classType->abstractMethods.insert(name);
            } else {
                // If it's a concrete implementation, remove it from the set of abstract methods
                classType->abstractMethods.erase(name);
            }

            auto methodType = std::make_shared<FunctionType>();
            if (!method->returnType.empty()) {
                // Handle 'this' return type for method chaining
                if (method->returnType == "this") {
                    methodType->returnType = classType;
                } else {
                    methodType->returnType = parseType(method->returnType, symbols);
                }
            } else {
                methodType->returnType = std::make_shared<Type>(TypeKind::Void);
            }

            if (method->isAsync) {
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

            for (const auto& param : method->parameters) {
                if (!param->type.empty()) {
                    methodType->paramTypes.push_back(parseType(param->type, symbols));
                } else {
                    methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
                methodType->isOptional.push_back(param->isOptional);
                if (param->isRest) methodType->hasRest = true;
            }
            if (method->isStatic) {
                if (method->hasBody) {
                    classType->staticMethods[name] = methodType;
                } else {
                    classType->staticMethodOverloads[name].push_back(methodType);
                }
                classType->staticMethodAccess[name] = method->access;
            } else if (method->isGetter) {
                classType->getters[name] = methodType;
                classType->methodAccess[name] = method->access;
            } else if (method->isSetter) {
                classType->setters[name] = methodType;
                classType->methodAccess[name] = method->access;
            } else {
                if (method->hasBody) {
                    classType->methods[name] = methodType;
                } else {
                    classType->methodOverloads[name].push_back(methodType);
                }
                classType->methodAccess[name] = method->access;
            }
        }
        if (auto staticBlock = dynamic_cast<StaticBlock*>(member.get())) {
            classType->staticBlocks.push_back(staticBlock);
        }
    }

    // Check if all abstract methods are implemented if this is a concrete class
    if (!classType->isAbstract && !classType->abstractMethods.empty()) {
        std::string missing = "";
        for (const auto& m : classType->abstractMethods) {
            missing += (missing.empty() ? "" : ", ") + m;
        }
        reportError(fmt::format("Class '{}' is not abstract and does not implement abstract methods: {}", node->name, missing));
    }

    // 4. Register constructor (if any) or default constructor
    auto ctorType = std::make_shared<FunctionType>();
    ctorType->returnType = classType;
    
    if (classType->methods.count("constructor")) {
        auto ctorMethod = classType->methods["constructor"];
        ctorType->paramTypes = ctorMethod->paramTypes;
    }
    classType->constructorOverloads.push_back(ctorType);
    
    // 5. Visit method bodies
    auto oldClass = currentClass;
    currentClass = classType;
    for (const auto& member : node->members) {
        if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            visitMethodDefinition(method, classType);
        } else if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            visitPropertyDefinition(prop, classType);
        } else if (auto staticBlock = dynamic_cast<StaticBlock*>(member.get())) {
            visitStaticBlock(staticBlock);
        }
    }
    currentClass = oldClass;

    // 6. Check interface implementations
    for (const auto& inter : classType->implementsInterfaces) {
        checkInterfaceImplementation(classType, inter);
    }

    symbols.exitScope();
    symbols.define(node->name, ctorType);
}

void Analyzer::checkInterfaceImplementation(std::shared_ptr<ClassType> classType, std::shared_ptr<InterfaceType> interfaceType) {
    // Check fields
    for (const auto& [name, type] : interfaceType->fields) {
        bool found = false;
        auto current = classType;
        while (current) {
            if (current->fields.count(name)) {
                found = true;
                break;
            }
            current = current->baseClass;
        }
        if (!found) {
            reportError(fmt::format("Class {} does not implement property {} from interface {}", classType->name, name, interfaceType->name));
        }
    }

    // Check methods
    for (const auto& [name, methodType] : interfaceType->methods) {
        if (classType->methods.find(name) == classType->methods.end()) {
            reportError(fmt::format("Class {} does not implement method {} from interface {}", classType->name, name, interfaceType->name));
        }
    }

    // Check base interfaces recursively
    for (const auto& base : interfaceType->baseInterfaces) {
        checkInterfaceImplementation(classType, base);
    }
}

void Analyzer::visitStaticBlock(ast::StaticBlock* node) {
    symbols.enterScope();
    if (currentClass) {
        auto ctorType = std::make_shared<FunctionType>();
        ctorType->returnType = currentClass;
        symbols.define("this", ctorType);
    }
    for (auto& stmt : node->body) {
        visit(stmt.get());
    }
    symbols.exitScope();
}

void Analyzer::visitPropertyDefinition(PropertyDefinition* node, std::shared_ptr<ClassType> classType) {
    if (node->initializer) {
        node->initializer->accept(this);
        auto type = lastType;
        std::string baseName = classType->originalName.empty() ? classType->name : classType->originalName;
        std::string name = manglePrivateName(node->name, baseName);
        SPDLOG_DEBUG("  Property {} inferred type: {}", name, type->toString());
        if (node->isStatic) {
            if (classType->staticFields.count(name)) {
                auto currentType = classType->staticFields[name];
                if (currentType->kind == TypeKind::Unknown || currentType->kind == TypeKind::Any) {
                    classType->staticFields[name] = type;
                }
            }
        } else {
            if (classType->fields.count(name)) {
                auto currentType = classType->fields[name];
                if (currentType->kind == TypeKind::Unknown || currentType->kind == TypeKind::Any) {
                    classType->fields[name] = type;
                }
            }
        }
    }
}

void Analyzer::visitInterfaceDeclaration(ast::InterfaceDeclaration* node) {
    auto interfaceType = std::make_shared<InterfaceType>(node->name);
    auto existing = symbols.lookupType(node->name);
    if (!existing || existing->kind != TypeKind::Interface) {
        symbols.defineType(node->name, interfaceType);
        if (node->isExported && currentModule) {
            currentModule->exports->defineType(node->name, interfaceType);
        }
        if (node->isDefaultExport && currentModule) {
            currentModule->exports->defineType("default", interfaceType);
        }
    } else {
        interfaceType = std::static_pointer_cast<InterfaceType>(existing);
    }

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
        interfaceType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    for (const auto& baseName : node->baseInterfaces) {
        auto base = symbols.lookupType(baseName);
        if (base && base->kind == TypeKind::Interface) {
            interfaceType->baseInterfaces.push_back(std::static_pointer_cast<InterfaceType>(base));
        }
    }

    for (const auto& member : node->members) {
        if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            interfaceType->fields[prop->name] = parseType(prop->type, symbols);
        } else if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            auto methodType = std::make_shared<FunctionType>();
            // Handle 'this' return type for method chaining
            if (method->returnType == "this") {
                methodType->returnType = interfaceType;
            } else {
                methodType->returnType = parseType(method->returnType, symbols);
            }
            for (const auto& param : method->parameters) {
                methodType->paramTypes.push_back(parseType(param->type, symbols));
            }
            interfaceType->methods[method->name] = methodType;
            interfaceType->methodOverloads[method->name].push_back(methodType);
        }
    }
    symbols.exitScope();
}

std::shared_ptr<ClassType> Analyzer::analyzeClassBody(ast::ClassDeclaration* node, const std::vector<std::shared_ptr<Type>>& typeArguments) {
    auto classType = std::make_shared<ClassType>(node->name);
    classType->originalName = node->name;
    
    // Copy inferred types from original class if it exists
    auto existing = symbols.lookupType(node->name);
    if (existing && existing->kind == TypeKind::Class) {
        auto existingClass = std::static_pointer_cast<ClassType>(existing);
        classType->staticFields = existingClass->staticFields;
        classType->fields = existingClass->fields;
        classType->staticFieldAccess = existingClass->staticFieldAccess;
        classType->fieldAccess = existingClass->fieldAccess;
        classType->staticReadonlyFields = existingClass->staticReadonlyFields;
        classType->readonlyFields = existingClass->readonlyFields;
        classType->staticComptimeFields = existingClass->staticComptimeFields;
        classType->comptimeFields = existingClass->comptimeFields;
        classType->staticComptimeMethods = existingClass->staticComptimeMethods;
        classType->comptimeMethods = existingClass->comptimeMethods;
    }

    if (!node->baseClass.empty()) {
        auto base = symbols.lookupType(node->baseClass);
        if (base && base->kind == TypeKind::Class) {
            classType->baseClass = std::static_pointer_cast<ClassType>(base);
        }
    }

    std::map<std::string, std::shared_ptr<Type>> env;
    for (size_t i = 0; i < node->typeParameters.size() && i < typeArguments.size(); ++i) {
        env[node->typeParameters[i]->name] = typeArguments[i];
    }

    symbols.enterScope();
    for (const auto& [name, type] : env) {
        symbols.defineType(name, type);
    }

    for (const auto& member : node->members) {
        if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
            auto propType = parseType(prop->type, symbols);
            std::string name = manglePrivateName(prop->name, node->name);
            if (prop->isStatic) {
                if (!classType->staticFields.count(name) || propType->kind != TypeKind::Unknown) {
                    classType->staticFields[name] = substitute(propType, env);
                }
            } else {
                if (!classType->fields.count(name) || propType->kind != TypeKind::Unknown) {
                    classType->fields[name] = substitute(propType, env);
                }
            }
        } else if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
            if (method->name == "constructor") {
                for (const auto& param : method->parameters) {
                    if (param->isParameterProperty) {
                        auto id = dynamic_cast<ast::Identifier*>(param->name.get());
                        if (id) {
                            auto fieldType = parseType(param->type, symbols);
                            std::string name = manglePrivateName(id->name, node->name);
                            classType->fields[name] = substitute(fieldType, env);
                            classType->fieldAccess[name] = param->access;
                        }
                    }
                }
            }
            auto methodType = std::make_shared<FunctionType>();
            for (const auto& p : method->parameters) {
                methodType->paramTypes.push_back(substitute(parseType(p->type, symbols), env));
            }
            // Handle 'this' return type for method chaining
            if (method->returnType == "this") {
                methodType->returnType = classType;
            } else {
                methodType->returnType = substitute(parseType(method->returnType, symbols), env);
            }
            std::string name = manglePrivateName(method->name, node->name);
            if (method->isStatic) {
                classType->staticMethods[name] = methodType;
            } else {
                classType->methods[name] = methodType;
            }
        }
    }

    symbols.exitScope();
    return classType;
}

std::shared_ptr<Type> Analyzer::analyzeMethodBody(ast::MethodDefinition* node, std::shared_ptr<ClassType> classType, const std::vector<std::shared_ptr<Type>>& typeArguments) {
    auto oldClass = currentClass;
    currentClass = classType;
    symbols.enterScope();
    
    // Define 'this'
    symbols.define("this", classType);

    // Define parameters
    for (const auto& p : node->parameters) {
        if (auto id = dynamic_cast<ast::Identifier*>(p->name.get())) {
            if (p->initializer) {
                visit(p->initializer.get());
                symbols.define(id->name, lastType);
            } else {
                symbols.define(id->name, parseType(p->type, symbols));
            }
        }
    }

    // Let's just analyze the body
    currentReturnType = std::make_shared<Type>(TypeKind::Any);
    std::shared_ptr<Type> inferredReturnType = std::make_shared<Type>(TypeKind::Void);
    functionDepth++;
    for (const auto& stmt : node->body) {
        visit(stmt.get());
        if (stmt->getKind() == "ReturnStatement") {
            inferredReturnType = lastType;
        }
    }
    functionDepth--;

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
    currentClass = oldClass;
    return inferredReturnType;
}

void Analyzer::visitClassExpression(ast::ClassExpression* node) {
    // If already analyzed (during re-analysis of function bodies), reuse existing type
    if (node->inferredType && node->inferredType->kind == TypeKind::Class) {
        lastType = node->inferredType;
        return;
    }

    // Generate a unique name for anonymous class expressions
    static int anonClassCounter = 0;
    std::string className = node->name.empty()
        ? "__anon_class_" + std::to_string(anonClassCounter++)
        : node->name;

    auto classType = std::make_shared<ClassType>(className);
    classType->originalName = className;
    classType->node = nullptr;
    classType->exprNode = node;  // Store the ClassExpression node for codegen

    // Register the class type
    symbols.defineType(className, classType);

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
        classType->typeParameters.push_back(tpType);
        symbols.defineType(tp->name, tpType);
    }

    // Resolve base class
    if (!node->baseClass.empty()) {
        auto base = symbols.lookupType(node->baseClass);
        if (!base || base->kind != TypeKind::Class) {
            reportError(fmt::format("Base class {} not found or not a class", node->baseClass));
        } else {
            classType->baseClass = std::static_pointer_cast<ClassType>(base);
            classType->abstractMethods = classType->baseClass->abstractMethods;
        }
    }

    // Resolve implemented interfaces
    for (const auto& interfaceName : node->implementsInterfaces) {
        auto interfaceType = symbols.lookupType(interfaceName);
        if (!interfaceType || interfaceType->kind != TypeKind::Interface) {
            reportError(fmt::format("Interface {} not found or not an interface", interfaceName));
        } else {
            classType->implementsInterfaces.push_back(std::static_pointer_cast<InterfaceType>(interfaceType));
        }
    }

    // Process class members - register method types first
    auto oldClass = currentClass;
    currentClass = classType;

    for (const auto& member : node->members) {
        if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
            auto propType = parseType(prop->type, symbols);
            std::string name = manglePrivateName(prop->name, className);
            if (prop->isStatic) {
                classType->staticFields[name] = propType;
                classType->staticFieldAccess[name] = prop->access;
                if (prop->isReadonly) {
                    classType->staticReadonlyFields.insert(name);
                }
            } else {
                classType->fields[name] = propType;
                classType->fieldAccess[name] = prop->access;
                if (prop->isReadonly) {
                    classType->readonlyFields.insert(name);
                }
            }
        } else if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
            // Register method type similar to ClassDeclaration
            std::string name = manglePrivateName(method->name, className);

            auto methodType = std::make_shared<FunctionType>();
            if (!method->returnType.empty()) {
                // Handle 'this' return type for method chaining
                if (method->returnType == "this") {
                    methodType->returnType = classType;
                } else {
                    methodType->returnType = parseType(method->returnType, symbols);
                }
            } else {
                methodType->returnType = std::make_shared<Type>(TypeKind::Void);
            }

            if (method->isAsync) {
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

            for (const auto& param : method->parameters) {
                if (!param->type.empty()) {
                    methodType->paramTypes.push_back(parseType(param->type, symbols));
                } else {
                    methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
                methodType->isOptional.push_back(param->isOptional);
                if (param->isRest) methodType->hasRest = true;
            }

            if (method->isStatic) {
                if (method->hasBody) {
                    classType->staticMethods[name] = methodType;
                } else {
                    classType->staticMethodOverloads[name].push_back(methodType);
                }
                classType->staticMethodAccess[name] = method->access;
            } else if (method->isGetter) {
                classType->getters[name] = methodType;
                classType->methodAccess[name] = method->access;
            } else if (method->isSetter) {
                classType->setters[name] = methodType;
                classType->methodAccess[name] = method->access;
            } else {
                if (method->hasBody) {
                    classType->methods[name] = methodType;
                } else {
                    classType->methodOverloads[name].push_back(methodType);
                }
                classType->methodAccess[name] = method->access;
            }
        }
    }

    // Register constructor (if any) or default constructor
    auto ctorType = std::make_shared<FunctionType>();
    ctorType->returnType = classType;

    if (classType->methods.count("constructor")) {
        auto ctorMethod = classType->methods["constructor"];
        ctorType->paramTypes = ctorMethod->paramTypes;
    }
    classType->constructorOverloads.push_back(ctorType);

    // Visit method bodies
    for (const auto& member : node->members) {
        if (auto method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
            visitMethodDefinition(method, classType);
        } else if (auto prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
            visitPropertyDefinition(prop, classType);
        }
    }

    symbols.exitScope();
    currentClass = oldClass;

    // The inferred type of a class expression is the class type itself
    node->inferredType = classType;
    lastType = classType;
}

} // namespace ts

