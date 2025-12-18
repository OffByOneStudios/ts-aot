#include "Analyzer.h"
#include "Monomorphizer.h"
#include <fmt/core.h>

namespace ts {

using namespace ast;

void Analyzer::visitCallExpression(CallExpression* node) {
    // 1. Evaluate arguments first to get their types for overload resolution
    std::vector<std::shared_ptr<Type>> argTypes;
    for (auto& arg : node->arguments) {
        visit(arg.get());
        argTypes.push_back(lastType);
    }

    // Resolve type arguments if any
    std::vector<std::shared_ptr<Type>> resolvedTypeArguments;
    for (const auto& typeName : node->typeArguments) {
        resolvedTypeArguments.push_back(parseType(typeName, symbols));
    }
    node->resolvedTypeArguments = resolvedTypeArguments;

    // 2. Evaluate callee
    visit(node->callee.get());
    auto calleeType = lastType;
    
    // Check for property access calls (methods)
    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
        if (prop->name == "charCodeAt") {
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "split") {
             lastType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::String));
             return;
        } else if (prop->name == "trim") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "substring") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "startsWith") {
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        } else if (prop->name == "includes") {
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        } else if (prop->name == "indexOf") {
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "toLowerCase") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "toUpperCase") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "sort") {
             lastType = std::make_shared<Type>(TypeKind::Void);
             return;
        } else if (prop->name == "slice") {
             visit(prop->expression.get());
             // lastType is now the type of the array, which is what slice returns
             return;
        } else if (prop->name == "join") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "set") {
             lastType = std::make_shared<Type>(TypeKind::Void);
             return;
        } else if (prop->name == "get") {
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "has") {
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        }
        
        // Check for Math methods
        if (auto obj = dynamic_cast<Identifier*>(prop->expression.get())) {
            if (obj->name == "Math") {
                if (prop->name == "min" || prop->name == "max" || prop->name == "abs" || prop->name == "floor" || prop->name == "ceil" || prop->name == "round") {
                    lastType = std::make_shared<Type>(TypeKind::Int);
                    return;
                } else if (prop->name == "sqrt" || prop->name == "pow" || prop->name == "random") {
                    lastType = std::make_shared<Type>(TypeKind::Double);
                    return;
                }
            } else if (obj->name == "JSON") {
                if (prop->name == "parse") {
                    lastType = std::make_shared<Type>(TypeKind::Any);
                    return;
                } else if (prop->name == "stringify") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    return;
                }
            } else if (obj->name == "fs") {
                if (prop->name == "readFileSync") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    return;
                } else if (prop->name == "writeFileSync") {
                    lastType = std::make_shared<Type>(TypeKind::Void);
                    return;
                } else if (prop->name == "promises") {
                    // Return a special type for fs.promises
                    lastType = std::make_shared<Type>(TypeKind::Any);
                    return;
                }
            } else if (obj->name == "crypto") {
                if (prop->name == "md5") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    return;
                }
            }
        }

        // Check for fs.promises.readFile
        if (auto innerProp = dynamic_cast<PropertyAccessExpression*>(prop->expression.get())) {
            if (auto obj = dynamic_cast<Identifier*>(innerProp->expression.get())) {
                if (obj->name == "fs" && innerProp->name == "promises") {
                    if (prop->name == "readFile") {
                        auto promiseType = std::make_shared<ClassType>("Promise");
                        promiseType->typeArguments.push_back(std::make_shared<Type>(TypeKind::String));
                        lastType = promiseType;
                        return;
                    }
                }
            }
        }

        // Check for class methods
        visit(prop->expression.get());
        auto objType = lastType;
        if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            std::shared_ptr<FunctionType> methodType = nullptr;
            
            if (cls->methodOverloads.count(prop->name)) {
                methodType = resolveOverload(cls->methodOverloads[prop->name], argTypes);
            } else if (cls->methods.count(prop->name)) {
                methodType = cls->methods[prop->name];
            }
            
            if (methodType) {
                lastType = methodType->returnType;
                return;
            }
        } else if (objType->kind == TypeKind::Interface) {
            auto inter = std::static_pointer_cast<InterfaceType>(objType);
            std::shared_ptr<FunctionType> methodType = nullptr;
            
            if (inter->methodOverloads.count(prop->name)) {
                methodType = resolveOverload(inter->methodOverloads[prop->name], argTypes);
            } else if (inter->methods.count(prop->name)) {
                methodType = inter->methods[prop->name];
            }
            
            if (methodType) {
                lastType = methodType->returnType;
                return;
            }
        } else if (objType->kind == TypeKind::Function) {
            auto func = std::static_pointer_cast<FunctionType>(objType);
            if (func->returnType && func->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(func->returnType);
                // Static method
                std::shared_ptr<FunctionType> methodType = nullptr;
                if (cls->staticMethodOverloads.count(prop->name)) {
                    methodType = resolveOverload(cls->staticMethodOverloads[prop->name], argTypes);
                } else if (cls->staticMethods.count(prop->name)) {
                    methodType = cls->staticMethods[prop->name];
                }
                
                if (methodType) {
                    lastType = methodType->returnType;
                    return;
                }
            }
        }
    }
    
    if (calleeType->kind == TypeKind::Function) {
        auto func = std::static_pointer_cast<FunctionType>(calleeType);
        
        if (resolvedTypeArguments.empty() && !func->typeParameters.empty()) {
            resolvedTypeArguments = inferTypeArguments(func->typeParameters, func->paramTypes, argTypes);
        }

        if (!resolvedTypeArguments.empty() && !func->typeParameters.empty()) {
            std::map<std::string, std::shared_ptr<Type>> env;
            for (size_t i = 0; i < func->typeParameters.size() && i < resolvedTypeArguments.size(); ++i) {
                auto param = func->typeParameters[i];
                auto arg = resolvedTypeArguments[i];
                if (param->constraint && !arg->isAssignableTo(param->constraint)) {
                    reportError(fmt::format("Type '{}' does not satisfy the constraint '{}' for type parameter '{}'", 
                        arg->toString(), param->constraint->toString(), param->name));
                }
                env[param->name] = arg;
            }
            lastType = substitute(func->returnType, env);
        } else {
            lastType = func->returnType;
        }
        
        if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
            functionUsages[id->name].push_back({argTypes, resolvedTypeArguments});
        }
        return;
    }

    std::string calleeName;
    if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        calleeName = id->name;
        
        auto sym = symbols.lookup(calleeName);
        if (sym) {
             if (sym->type->kind == TypeKind::Function) {
                 auto funcType = std::static_pointer_cast<FunctionType>(sym->type);
                 lastType = funcType->returnType;
             }
        }

        if (calleeName == "parseInt") {
             for (auto& arg : node->arguments) visit(arg.get());
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        }

        if (calleeName == "fetch") {
             for (auto& arg : node->arguments) visit(arg.get());
             auto promiseType = std::make_shared<ClassType>("Promise");
             promiseType->typeArguments.push_back(std::make_shared<Type>(TypeKind::Any));
             lastType = promiseType;
             return;
        }
    }

    if (!calleeName.empty()) {
        functionUsages[calleeName].push_back({argTypes, resolvedTypeArguments});
    }
    
    // For now, assume calls return Any unless we know better
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitNewExpression(NewExpression* node) {
    std::vector<std::shared_ptr<Type>> ctorArgTypes;
    for (auto& arg : node->arguments) {
        visit(arg.get());
        ctorArgTypes.push_back(lastType);
    }

    // Resolve type arguments if any
    std::vector<std::shared_ptr<Type>> resolvedTypeArguments;
    for (const auto& typeName : node->typeArguments) {
        resolvedTypeArguments.push_back(parseType(typeName, symbols));
    }
    node->resolvedTypeArguments = resolvedTypeArguments;

    visit(node->expression.get());
    
    if (auto id = dynamic_cast<Identifier*>(node->expression.get())) {
        if (id->name == "Map") {
            lastType = std::make_shared<Type>(TypeKind::Map);
            return;
        } else if (id->name == "Array") {
            lastType = std::make_shared<ArrayType>(std::make_shared<Type>(TypeKind::Any));
            return;
        }

        // Check for user-defined classes
        auto type = symbols.lookupType(id->name);
        if (type && type->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(type);
            if (!resolvedTypeArguments.empty() && !classType->typeParameters.empty() && classType->node) {
                // Validate constraints
                for (size_t i = 0; i < classType->typeParameters.size() && i < resolvedTypeArguments.size(); ++i) {
                    auto param = classType->typeParameters[i];
                    auto arg = resolvedTypeArguments[i];
                    if (param->constraint && !arg->isAssignableTo(param->constraint)) {
                        reportError(fmt::format("Type '{}' does not satisfy the constraint '{}' for type parameter '{}'", 
                            arg->toString(), param->constraint->toString(), param->name));
                    }
                }
                auto specType = analyzeClassBody(classType->node, resolvedTypeArguments);
                specType->name = Monomorphizer::generateMangledName(classType->name, {}, resolvedTypeArguments);
                lastType = specType;
            } else {
                lastType = type;
            }
            classUsages[id->name].push_back(resolvedTypeArguments);
            return;
        }

        auto sym = symbols.lookup(id->name);
        if (sym && sym->type->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(sym->type);
            if (funcType->returnType->kind == TypeKind::Class) {
                auto classType = std::static_pointer_cast<ClassType>(funcType->returnType);
                if (classType->isAbstract) {
                    reportError(fmt::format("Cannot create an instance of an abstract class '{}'", classType->name));
                }

                // Resolve constructor overload
                if (!classType->constructorOverloads.empty()) {
                    auto resolvedCtor = resolveOverload(classType->constructorOverloads, ctorArgTypes);
                    if (!resolvedCtor) {
                        reportError(fmt::format("No constructor overload for '{}' matches arguments", classType->name));
                    }
                }

                lastType = classType;
                return;
            }
        }
    }
    
    lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitObjectLiteralExpression(ObjectLiteralExpression* node) {
    auto objType = std::make_shared<ObjectType>();
    for (auto& prop : node->properties) {
        visit(prop->initializer.get());
        objType->fields[prop->name] = lastType;
    }
    lastType = objType;
}

void Analyzer::visitArrayLiteralExpression(ArrayLiteralExpression* node) {
    std::shared_ptr<Type> elemType = nullptr;
    for (auto& el : node->elements) {
        visit(el.get());
        if (!elemType) {
            elemType = lastType;
        }
        // TODO: Check for mixed types and upgrade to Any
    }
    if (!elemType) elemType = std::make_shared<Type>(TypeKind::Any);
    lastType = std::make_shared<ArrayType>(elemType);
}

void Analyzer::visitElementAccessExpression(ElementAccessExpression* node) {
    visit(node->expression.get());
    auto arrayType = std::dynamic_pointer_cast<ArrayType>(lastType);
    
    visit(node->argumentExpression.get());
    // TODO: Verify index is integer
    
    if (arrayType) {
        lastType = arrayType->elementType;
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitPropertyAccessExpression(PropertyAccessExpression* node) {
    if (auto id = dynamic_cast<Identifier*>(node->expression.get())) {
        if (id->name == "Math" && node->name == "PI") {
            lastType = std::make_shared<Type>(TypeKind::Double);
            return;
        }
    }

    visit(node->expression.get());
    auto objType = lastType;

    while (objType && objType->kind == TypeKind::TypeParameter) {
        auto tp = std::static_pointer_cast<TypeParameterType>(objType);
        if (tp->constraint) {
            objType = tp->constraint;
        } else {
            objType = std::make_shared<Type>(TypeKind::Any);
            break;
        }
    }
    
    if (node->name == "length" && (objType->kind == TypeKind::String || objType->kind == TypeKind::Array)) {
        lastType = std::make_shared<Type>(TypeKind::Int);
        return;
    }

    if (node->name == "size" && objType->kind == TypeKind::Map) {
        lastType = std::make_shared<Type>(TypeKind::Int);
        return;
    }

    if (objType->kind == TypeKind::Namespace) {
        auto ns = std::static_pointer_cast<NamespaceType>(objType);
        auto sym = ns->module->exports->lookup(node->name);
        if (sym) {
            lastType = sym->type;
            return;
        }
        auto type = ns->module->exports->lookupType(node->name);
        if (type) {
            lastType = type;
            return;
        }
        reportError(fmt::format("Module does not export {}", node->name));
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }

    if (objType->kind == TypeKind::Union) {
            auto unionType = std::static_pointer_cast<UnionType>(objType);
            std::vector<std::shared_ptr<Type>> memberTypes;
            for (auto& t : unionType->types) {
                // Recursively check each type in the union
                lastType = t;
                // We need a way to check property access without side effects or with a way to restore state
                // For now, let's assume we can just check if it's an object/class/interface
                std::shared_ptr<Type> foundType = nullptr;
                if (t->kind == TypeKind::Object) {
                    auto obj = std::static_pointer_cast<ObjectType>(t);
                    if (obj->fields.count(node->name)) foundType = obj->fields[node->name];
                } else if (t->kind == TypeKind::Class) {
                    auto cls = std::static_pointer_cast<ClassType>(t);
                    auto current = cls;
                    while (current) {
                        if (current->fields.count(node->name)) { foundType = current->fields[node->name]; break; }
                        if (current->methods.count(node->name)) { foundType = current->methods[node->name]; break; }
                        current = current->baseClass;
                    }
                } else if (t->kind == TypeKind::Interface) {
                    auto inter = std::static_pointer_cast<InterfaceType>(t);
                    if (inter->fields.count(node->name)) foundType = inter->fields[node->name];
                    else if (inter->methods.count(node->name)) foundType = inter->methods[node->name];
                }

                if (!foundType) {
                    // Property not found in one of the union members
                    lastType = std::make_shared<Type>(TypeKind::Any);
                    return;
                }
                memberTypes.push_back(foundType);
            }
            if (memberTypes.size() == 1) lastType = memberTypes[0];
            else lastType = std::make_shared<UnionType>(memberTypes);
            return;
        } else if (objType->kind == TypeKind::Intersection) {
            auto interType = std::static_pointer_cast<IntersectionType>(objType);
            std::vector<std::shared_ptr<Type>> memberTypes;
            for (auto& t : interType->types) {
                std::shared_ptr<Type> foundType = nullptr;
                if (t->kind == TypeKind::Object) {
                    auto obj = std::static_pointer_cast<ObjectType>(t);
                    if (obj->fields.count(node->name)) foundType = obj->fields[node->name];
                } else if (t->kind == TypeKind::Class) {
                    auto cls = std::static_pointer_cast<ClassType>(t);
                    auto current = cls;
                    while (current) {
                        if (current->fields.count(node->name)) { foundType = current->fields[node->name]; break; }
                        if (current->methods.count(node->name)) { foundType = current->methods[node->name]; break; }
                        current = current->baseClass;
                    }
                } else if (t->kind == TypeKind::Interface) {
                    auto inter = std::static_pointer_cast<InterfaceType>(t);
                    if (inter->fields.count(node->name)) foundType = inter->fields[node->name];
                    else if (inter->methods.count(node->name)) foundType = inter->methods[node->name];
                }
                if (foundType) memberTypes.push_back(foundType);
            }
            if (memberTypes.empty()) lastType = std::make_shared<Type>(TypeKind::Any);
            else if (memberTypes.size() == 1) lastType = memberTypes[0];
            else lastType = std::make_shared<IntersectionType>(memberTypes);
            return;
        }

        if (objType->kind == TypeKind::Object) {
            auto obj = std::static_pointer_cast<ObjectType>(objType);
            if (obj->fields.count(node->name)) {
                lastType = obj->fields[node->name];
                return;
            }
        } else if (objType->kind == TypeKind::Interface) {
            auto inter = std::static_pointer_cast<InterfaceType>(objType);
            if (inter->fields.count(node->name)) {
                lastType = inter->fields[node->name];
                fmt::print("Found property {} on interface {} with type {}\n", node->name, inter->name, lastType->toString());
                return;
            } else if (inter->methods.count(node->name)) {
                lastType = inter->methods[node->name];
                return;
            }
            // TODO: Check base interfaces
        } else if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            
            // Find defining class and access modifier
            std::shared_ptr<ClassType> definingClass = nullptr;
            AccessModifier access = AccessModifier::Public;
            std::shared_ptr<Type> memberType = nullptr;

            auto current = cls;
            while (current) {
                if (current->fields.count(node->name)) {
                    definingClass = current;
                    access = current->fieldAccess[node->name];
                    memberType = current->fields[node->name];
                    break;
                } else if (current->methods.count(node->name)) {
                    definingClass = current;
                    access = current->methodAccess[node->name];
                    memberType = current->methods[node->name];
                    break;
                } else if (current->getters.count(node->name)) {
                    definingClass = current;
                    access = current->methodAccess[node->name];
                    memberType = current->getters[node->name]->returnType;
                    break;
                }
                current = current->baseClass;
            }

            if (definingClass) {
                // Visibility check
                bool allowed = true;
                if (access == AccessModifier::Private) {
                    if (currentClass != definingClass) {
                        reportError(fmt::format("Property {} is private and not accessible from here", node->name));
                    }
                } else if (access == AccessModifier::Protected) {
                    if (!currentClass || !currentClass->isSubclassOf(definingClass)) {
                        reportError(fmt::format("Property {} is protected and not accessible from here", node->name));
                    }
                }
                lastType = memberType;
                return;
            }
        } else if (objType->kind == TypeKind::Function) {
            auto funcType = std::static_pointer_cast<FunctionType>(objType);
            if (funcType->returnType && funcType->returnType->kind == TypeKind::Class) {
                auto cls = std::static_pointer_cast<ClassType>(funcType->returnType);
                
                // Static access
                std::shared_ptr<ClassType> definingClass = nullptr;
                AccessModifier access = AccessModifier::Public;
                std::shared_ptr<Type> memberType = nullptr;

                auto current = cls;
                while (current) {
                    if (current->staticFields.count(node->name)) {
                        definingClass = current;
                        access = current->staticFieldAccess[node->name];
                        memberType = current->staticFields[node->name];
                        break;
                    } else if (current->staticMethods.count(node->name)) {
                        definingClass = current;
                        access = current->staticMethodAccess[node->name];
                        memberType = current->staticMethods[node->name];
                        break;
                    }
                    current = current->baseClass;
                }

                if (definingClass) {
                    if (access == AccessModifier::Private) {
                        if (currentClass != definingClass) {
                            reportError(fmt::format("Static property {} is private and not accessible from here", node->name));
                        }
                    } else if (access == AccessModifier::Protected) {
                        if (!currentClass || !currentClass->isSubclassOf(definingClass)) {
                            reportError(fmt::format("Static property {} is protected and not accessible from here", node->name));
                        }
                    }
                    lastType = memberType;
                    return;
                }
            }
        } else if (objType->kind == TypeKind::Any) {
            lastType = std::make_shared<Type>(TypeKind::Any);
            return;
        }

        // Built-in methods fallback
        if (node->name == "includes" || node->name == "indexOf" || node->name == "toLowerCase" || node->name == "toUpperCase" || node->name == "slice" || node->name == "join") {
            lastType = std::make_shared<Type>(TypeKind::Any); // Or more specific function type
            return;
        }

        reportError(fmt::format("Unknown property {}", node->name));
        lastType = std::make_shared<Type>(TypeKind::Any);
}

void Analyzer::visitBinaryExpression(BinaryExpression* node) {
    if (node->op == "instanceof") {
        visit(node->left.get());
        visit(node->right.get());
        lastType = std::make_shared<Type>(TypeKind::Boolean);
        return;
    }
    visit(node->left.get());
    auto leftType = lastType;
    visit(node->right.get());
    auto rightType = lastType;

    // Simple type inference for binary ops
    if (leftType && rightType) {
        if (leftType->kind == TypeKind::Int && rightType->kind == TypeKind::Int) {
            lastType = std::make_shared<Type>(TypeKind::Int);
        } else if (leftType->isNumber() && rightType->isNumber()) {
            lastType = std::make_shared<Type>(TypeKind::Double);
        } else if (leftType->kind == TypeKind::String || rightType->kind == TypeKind::String) {
            lastType = std::make_shared<Type>(TypeKind::String);
        } else {
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    } else {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitAssignmentExpression(AssignmentExpression* node) {
    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->left.get())) {
        visit(prop->expression.get());
        auto objType = lastType;
        if (objType->kind == TypeKind::Class) {
            auto cls = std::static_pointer_cast<ClassType>(objType);
            auto current = cls;
            while (current) {
                if (current->fields.count(prop->name)) {
                    if (current->readonlyFields.count(prop->name)) {
                        // Only allowed in constructor of the same class
                        if (currentMethodName != "constructor" || currentClass != current) {
                            reportError(fmt::format("Cannot assign to '{}' because it is a read-only property.", prop->name));
                        }
                    }
                    break;
                } else if (current->staticFields.count(prop->name)) {
                    if (current->staticReadonlyFields.count(prop->name)) {
                        reportError(fmt::format("Cannot assign to static '{}' because it is a read-only property.", prop->name));
                    }
                    break;
                }
                current = current->baseClass;
            }
        }
    }

    visit(node->left.get());
    visit(node->right.get());
}

void Analyzer::visitIdentifier(ast::Identifier* node) {
    auto sym = symbols.lookup(node->name);
    if (sym) {
        lastType = sym->type;
    } else {
        // Check if it's a class name
        auto type = symbols.lookupType(node->name);
        if (type && type->kind == TypeKind::Class) {
            auto ctorType = std::make_shared<FunctionType>();
            ctorType->returnType = type;
            lastType = ctorType;
        } else if (type) {
            lastType = type;
        } else {
            fmt::print("Failed to find identifier: {}\n", node->name);
            reportError("Undefined variable " + node->name);
            lastType = std::make_shared<Type>(TypeKind::Any);
        }
    }
}

void Analyzer::visitStringLiteral(ast::StringLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::String);
}

void Analyzer::visitNumericLiteral(ast::NumericLiteral* node) {
    if (node->value == (int)node->value) {
        lastType = std::make_shared<Type>(TypeKind::Int);
    } else {
        lastType = std::make_shared<Type>(TypeKind::Double);
    }
}

void Analyzer::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastType = std::make_shared<Type>(TypeKind::Boolean);
}

void Analyzer::visitAwaitExpression(ast::AwaitExpression* node) {
    visit(node->expression.get());
    auto type = lastType;
    if (type->kind == TypeKind::Class) {
        auto cls = std::static_pointer_cast<ClassType>(type);
        if (cls->name == "Promise" && !cls->typeArguments.empty()) {
            lastType = cls->typeArguments[0];
            return;
        }
    }
    // If it's not a promise, await just returns the value (simplified)
    lastType = type;
}

void Analyzer::visitTemplateExpression(ast::TemplateExpression* node) {
    for (auto& span : node->spans) {
        visit(span.expression.get());
    }
    lastType = std::make_shared<Type>(TypeKind::String);
}

void Analyzer::visitPrefixUnaryExpression(PrefixUnaryExpression* node) {
    visit(node->operand.get());
}

void Analyzer::visitPostfixUnaryExpression(PostfixUnaryExpression* node) {
    visit(node->operand.get());
}

void Analyzer::visitSuperExpression(SuperExpression* node) {
    if (currentClass && currentClass->baseClass) {
        lastType = currentClass->baseClass;
    } else {
        reportError("'super' used outside of a derived class");
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitAsExpression(AsExpression* node) {
    visit(node->expression.get());
    lastType = parseType(node->type, symbols);
    node->inferredType = lastType;
}

std::vector<std::shared_ptr<Type>> Analyzer::inferTypeArguments(
    const std::vector<std::shared_ptr<TypeParameterType>>& typeParams,
    const std::vector<std::shared_ptr<Type>>& paramTypes,
    const std::vector<std::shared_ptr<Type>>& argTypes) {
    
    std::map<std::string, std::shared_ptr<Type>> inferred;
    
    auto inferFromTypes = [&](auto self, std::shared_ptr<Type> paramType, std::shared_ptr<Type> argType) -> void {
        if (!paramType || !argType) return;

        // printf("Inferring from param: %s and arg: %s\n", paramType->toString().c_str(), argType->toString().c_str());

        if (paramType->kind == TypeKind::TypeParameter) {
            auto tp = std::static_pointer_cast<TypeParameterType>(paramType);
            bool target = false;
            for (auto& p : typeParams) {
                if (p->name == tp->name) { target = true; break; }
            }
            if (target) {
                if (inferred.find(tp->name) == inferred.end()) {
                    inferred[tp->name] = argType;
                }
            }
        } else if (paramType->kind == TypeKind::Array && argType->kind == TypeKind::Array) {
            auto pa = std::static_pointer_cast<ArrayType>(paramType);
            auto aa = std::static_pointer_cast<ArrayType>(argType);
            self(self, pa->elementType, aa->elementType);
        }
    };

    size_t count = std::min(paramTypes.size(), argTypes.size());
    for (size_t i = 0; i < count; ++i) {
        inferFromTypes(inferFromTypes, paramTypes[i], argTypes[i]);
    }

    std::vector<std::shared_ptr<Type>> result;
    for (auto& tp : typeParams) {
        if (inferred.count(tp->name)) {
            result.push_back(inferred[tp->name]);
        } else {
            // Fallback to Any or constraint if not inferred
            result.push_back(tp->constraint ? tp->constraint : std::make_shared<Type>(TypeKind::Any));
        }
    }
    return result;
}

} // namespace ts
