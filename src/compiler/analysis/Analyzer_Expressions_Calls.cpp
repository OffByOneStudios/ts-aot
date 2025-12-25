#include "Analyzer.h"
#include "Monomorphizer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {
using namespace ast;
void Analyzer::visitCallExpression(ast::CallExpression* node) {
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

    if (calleeType->kind == TypeKind::Null || calleeType->kind == TypeKind::Undefined) {
        if (node->isOptional) {
            lastType = std::make_shared<Type>(TypeKind::Undefined);
            node->inferredType = lastType;
            return;
        }
        reportError(fmt::format("Callee is possibly '{}'.", calleeType->kind == TypeKind::Null ? "null" : "undefined"));
        lastType = std::make_shared<Type>(TypeKind::Any);
        return;
    }

    std::string calleeName;
    if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        if (id->name == "BigInt") {
            lastType = std::make_shared<Type>(TypeKind::BigInt);
            node->inferredType = lastType;
            return;
        }
        if (id->name == "Symbol") {
            lastType = std::make_shared<Type>(TypeKind::Symbol);
            node->inferredType = lastType;
            return;
        }
        calleeName = id->name;
    }
    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
        calleeName = prop->name;
    }
    SPDLOG_DEBUG("Analyzer::visitCallExpression: {}", calleeName);
    
    // Check for ts_aot.comptime
    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
        if (auto id = dynamic_cast<Identifier*>(prop->expression.get())) {
            if (id->name == "ts_aot" && prop->name == "comptime") {
                node->isComptime = true;
                if (node->arguments.size() > 0) {
                    visit(node->arguments[0].get());
                    if (lastType->kind == TypeKind::Function) {
                        auto funcType = std::static_pointer_cast<FunctionType>(lastType);
                        lastType = funcType->returnType;
                        return;
                    }
                }
            }
        }
    }

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
        } else if (prop->name == "replace" || prop->name == "replaceAll") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "repeat") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "padStart" || prop->name == "padEnd") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "sort") {
             lastType = std::make_shared<Type>(TypeKind::Void);
             return;
        } else if (prop->name == "forEach") {
             lastType = std::make_shared<Type>(TypeKind::Void);
             return;
        } else if (prop->name == "map") {
             visit(prop->expression.get());
             // For now, assume map returns the same array type. 
             // In a real compiler we'd check the callback return type.
             return;
        } else if (prop->name == "filter") {
             visit(prop->expression.get());
             return;
        } else if (prop->name == "reduce") {
             // For now, assume reduce returns the same type as the array elements or initial value
             lastType = std::make_shared<Type>(TypeKind::Any);
             return;
        } else if (prop->name == "some" || prop->name == "every") {
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        } else if (prop->name == "find") {
             visit(prop->expression.get());
             if (lastType->kind == TypeKind::Array) {
                 lastType = std::static_pointer_cast<ArrayType>(lastType)->elementType;
             }
             return;
        } else if (prop->name == "findIndex") {
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "flat" || prop->name == "flatMap") {
             visit(prop->expression.get());
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
                if (prop->name == "min" || prop->name == "max" || prop->name == "abs" || prop->name == "floor" || prop->name == "ceil" || prop->name == "round" || prop->name == "clz32") {
                    lastType = std::make_shared<Type>(TypeKind::Int);
                    return;
                } else if (prop->name == "sqrt" || prop->name == "pow" || prop->name == "random" ||
                           prop->name == "log10" || prop->name == "log2" || prop->name == "log1p" ||
                           prop->name == "expm1" || prop->name == "cosh" || prop->name == "sinh" ||
                           prop->name == "tanh" || prop->name == "acosh" || prop->name == "asinh" ||
                           prop->name == "atanh" || prop->name == "cbrt" || prop->name == "hypot" ||
                           prop->name == "trunc" || prop->name == "fround") {
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
                } else if (prop->name == "existsSync") {
                    lastType = std::make_shared<Type>(TypeKind::Boolean);
                    return;
                } else if (prop->name == "mkdirSync" || prop->name == "rmdirSync" || prop->name == "unlinkSync") {
                    lastType = std::make_shared<Type>(TypeKind::Void);
                    return;
                } else if (prop->name == "statSync") {
                    auto statsType = std::make_shared<ObjectType>();
                    statsType->fields["size"] = std::make_shared<Type>(TypeKind::Int);
                    statsType->fields["mtimeMs"] = std::make_shared<Type>(TypeKind::Double);
                    
                    auto isFileType = std::make_shared<FunctionType>();
                    isFileType->returnType = std::make_shared<Type>(TypeKind::Boolean);
                    statsType->fields["isFile"] = isFileType;

                    auto isDirectoryType = std::make_shared<FunctionType>();
                    isDirectoryType->returnType = std::make_shared<Type>(TypeKind::Boolean);
                    statsType->fields["isDirectory"] = isDirectoryType;
                    
                    lastType = statsType;
                    return;
                }
            } else if (obj->name == "process") {
                if (prop->name == "cwd") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    return;
                } else if (prop->name == "exit") {
                    lastType = std::make_shared<Type>(TypeKind::Void);
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
                    } else if (prop->name == "writeFile" || prop->name == "mkdir") {
                        auto promiseType = std::make_shared<ClassType>("Promise");
                        promiseType->typeArguments.push_back(std::make_shared<Type>(TypeKind::Void));
                        lastType = promiseType;
                        return;
                    } else if (prop->name == "stat") {
                        auto statsType = std::make_shared<ObjectType>();
                        statsType->fields["size"] = std::make_shared<Type>(TypeKind::Int);
                        statsType->fields["mtimeMs"] = std::make_shared<Type>(TypeKind::Double);
                        
                        auto isFileType = std::make_shared<FunctionType>();
                        isFileType->returnType = std::make_shared<Type>(TypeKind::Boolean);
                        statsType->fields["isFile"] = isFileType;

                        auto isDirectoryType = std::make_shared<FunctionType>();
                        isDirectoryType->returnType = std::make_shared<Type>(TypeKind::Boolean);
                        statsType->fields["isDirectory"] = isDirectoryType;

                        auto promiseType = std::make_shared<ClassType>("Promise");
                        promiseType->typeArguments.push_back(statsType);
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
        } else if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
            if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Namespace) {
                functionUsages[prop->name].push_back({argTypes, resolvedTypeArguments});
            }
        }
        return;
    }

    if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        calleeName = id->name;
    } else if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
        if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Namespace) {
            calleeName = prop->name;
        }
    }

    if (!calleeName.empty()) {
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
    
    // If we haven't determined a more specific type, default to Any
    if (!lastType) {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitNewExpression(ast::NewExpression* node) {
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
            node->inferredType = lastType;
            return;
        } else if (id->name == "Set") {
            lastType = std::make_shared<Type>(TypeKind::SetType);
            node->inferredType = lastType;
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
            node->inferredType = lastType;
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

} // namespace ts


