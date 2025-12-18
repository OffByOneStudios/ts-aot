#include "Analyzer.h"
#include <fmt/core.h>

namespace ts {

using namespace ast;

void Analyzer::visitClassDeclaration(ClassDeclaration* node) {
    auto classType = std::make_shared<ClassType>(node->name);
    classType->isAbstract = node->isAbstract;
    
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

    // 2. Register class type
    symbols.defineType(node->name, classType);

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
        if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            auto propType = parseType(prop->type, symbols);
            if (prop->isStatic) {
                classType->staticFields[prop->name] = propType;
                classType->staticFieldAccess[prop->name] = prop->access;
                if (prop->isReadonly) {
                    classType->staticReadonlyFields.insert(prop->name);
                }
            } else {
                classType->fields[prop->name] = propType;
                classType->fieldAccess[prop->name] = prop->access;
                if (prop->isReadonly) {
                    classType->readonlyFields.insert(prop->name);
                }
            }
        }
        if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            if (method->name == "constructor") {
                for (const auto& param : method->parameters) {
                    if (param->isParameterProperty) {
                        auto fieldType = parseType(param->type, symbols);
                        classType->fields[param->name] = fieldType;
                        classType->fieldAccess[param->name] = param->access;
                        if (param->isReadonly) {
                            classType->readonlyFields.insert(param->name);
                        }
                    }
                }
            }
            if (method->isAbstract) {
                if (!node->isAbstract) {
                    reportError(fmt::format("Abstract method '{}' can only be declared in an abstract class.", method->name));
                }
                classType->abstractMethods.insert(method->name);
            } else {
                // If it's a concrete implementation, remove it from the set of abstract methods
                classType->abstractMethods.erase(method->name);
            }

            auto methodType = std::make_shared<FunctionType>();
            if (!method->returnType.empty()) {
                methodType->returnType = parseType(method->returnType, symbols);
            } else {
                methodType->returnType = std::make_shared<Type>(TypeKind::Void);
            }
            for (const auto& param : method->parameters) {
                if (!param->type.empty()) {
                    methodType->paramTypes.push_back(parseType(param->type, symbols));
                } else {
                    methodType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
                }
            }
            if (method->isStatic) {
                if (method->hasBody) {
                    classType->staticMethods[method->name] = methodType;
                } else {
                    classType->staticMethodOverloads[method->name].push_back(methodType);
                }
                classType->staticMethodAccess[method->name] = method->access;
            } else if (method->isGetter) {
                classType->getters[method->name] = methodType;
                classType->methodAccess[method->name] = method->access;
            } else if (method->isSetter) {
                classType->setters[method->name] = methodType;
                classType->methodAccess[method->name] = method->access;
            } else {
                if (method->hasBody) {
                    classType->methods[method->name] = methodType;
                } else {
                    classType->methodOverloads[method->name].push_back(methodType);
                }
                classType->methodAccess[method->name] = method->access;
            }
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
    
    symbols.define(node->name, ctorType);

    // 5. Visit method bodies
    auto oldClass = currentClass;
    currentClass = classType;
    for (const auto& member : node->members) {
        if (auto method = dynamic_cast<MethodDefinition*>(member.get())) {
            visitMethodDefinition(method, classType);
        } else if (auto prop = dynamic_cast<PropertyDefinition*>(member.get())) {
            visitPropertyDefinition(prop, classType);
        }
    }
    currentClass = oldClass;

    // 6. Check interface implementations
    for (const auto& inter : classType->implementsInterfaces) {
        checkInterfaceImplementation(classType, inter);
    }
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

void Analyzer::visitPropertyDefinition(PropertyDefinition* node, std::shared_ptr<ClassType> classType) {
    if (node->initializer) {
        visit(node->initializer.get());
        // TODO: Check type compatibility
    }
}

void Analyzer::visitInterfaceDeclaration(InterfaceDeclaration* node) {
    auto interfaceType = std::make_shared<InterfaceType>(node->name);
    symbols.defineType(node->name, interfaceType);

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
            methodType->returnType = parseType(method->returnType, symbols);
            for (const auto& param : method->parameters) {
                methodType->paramTypes.push_back(parseType(param->type, symbols));
            }
            interfaceType->methods[method->name] = methodType;
            interfaceType->methodOverloads[method->name].push_back(methodType);
        }
    }
}

} // namespace ts
