#include "Analyzer.h"
#include "Monomorphizer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {
using namespace ast;

// Helper to get expected callback type for known APIs
static std::shared_ptr<FunctionType> getExpectedCallbackType(const std::string& objName, const std::string& methodName, size_t argIndex) {
    // http.createServer / https.createServer callback: (req: IncomingMessage, res: ServerResponse) => void
    if ((objName == "http" || objName == "https") && methodName == "createServer" && argIndex == 0) {
        auto cbType = std::make_shared<FunctionType>();
        auto reqType = std::make_shared<ClassType>("IncomingMessage");
        auto resType = std::make_shared<ClassType>("ServerResponse");
        cbType->paramTypes.push_back(reqType);
        cbType->paramTypes.push_back(resType);
        cbType->returnType = std::make_shared<Type>(TypeKind::Void);
        return cbType;
    }
    // net.createServer callback: (socket: Socket) => void
    if (objName == "net" && methodName == "createServer" && argIndex == 0) {
        auto cbType = std::make_shared<FunctionType>();
        auto socketType = std::make_shared<ClassType>("Socket");
        cbType->paramTypes.push_back(socketType);
        cbType->returnType = std::make_shared<Type>(TypeKind::Void);
        return cbType;
    }
    // EventEmitter.on('data', callback) - callback receives chunk
    if (methodName == "on" && argIndex == 1) {
        auto cbType = std::make_shared<FunctionType>();
        cbType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // chunk/event data
        cbType->returnType = std::make_shared<Type>(TypeKind::Void);
        return cbType;
    }
    // fs.readFile callback: (err, data) => void
    if (objName == "fs" && (methodName == "readFile" || methodName == "writeFile") && argIndex == 1) {
        auto cbType = std::make_shared<FunctionType>();
        cbType->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any)); // err
        cbType->paramTypes.push_back(std::make_shared<ClassType>("Buffer")); // data
        cbType->returnType = std::make_shared<Type>(TypeKind::Void);
        return cbType;
    }
    return nullptr;
}

void Analyzer::visitCallExpression(ast::CallExpression* node) {
    std::vector<std::shared_ptr<Type>> argTypes;
    if (currentModule && currentModule->type == ModuleType::UntypedJavaScript) {
        // Skip type checking for JS but still record usage
        visit(node->callee.get());
        for (auto& arg : node->arguments) {
            visit(arg.get());
            argTypes.push_back(std::make_shared<Type>(TypeKind::Any));
        }

        std::string calleeName;
        if (auto id = dynamic_cast<ast::Identifier*>(node->callee.get())) {
            calleeName = id->name;
        }

        if (!calleeName.empty()) {
            functionUsages[calleeName].push_back({argTypes, {}});
        }

        lastType = std::make_shared<Type>(TypeKind::Any);
        node->inferredType = lastType;
        return;
    }

    // First, get the callee type to determine expected parameter types
    visit(node->callee.get());
    auto calleeType = lastType;
    
    // Extract object and method names for contextual typing lookup
    std::string objName;
    std::string methodName;
    if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
        methodName = prop->name;
        if (auto id = dynamic_cast<Identifier*>(prop->expression.get())) {
            objName = id->name;
        }
    } else if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
        methodName = id->name;
    }
    
    // Get expected parameter types from callee
    std::vector<std::shared_ptr<Type>> expectedParamTypes;
    if (calleeType && calleeType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(calleeType);
        expectedParamTypes = funcType->paramTypes;
    }
    
    // Check if callee has a rest parameter
    bool calleeHasRest = false;
    if (calleeType && calleeType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(calleeType);
        calleeHasRest = funcType->hasRest;
    }
    
    // 1. Evaluate arguments with contextual typing for arrow functions
    for (size_t i = 0; i < node->arguments.size(); i++) {
        auto& arg = node->arguments[i];
        
        // Handle spread elements - expand array element types into individual args
        if (auto spread = dynamic_cast<SpreadElement*>(arg.get())) {
            visit(spread->expression.get());
            auto spreadType = lastType;
            
            // Get the element type from the array
            std::shared_ptr<Type> elementType = std::make_shared<Type>(TypeKind::Any);
            if (spreadType && spreadType->kind == TypeKind::Array) {
                auto arrType = std::static_pointer_cast<ArrayType>(spreadType);
                elementType = arrType->elementType;
            }
            
            // Calculate how many remaining parameters the function expects
            size_t remainingParams = expectedParamTypes.size() > argTypes.size() 
                ? expectedParamTypes.size() - argTypes.size() 
                : 0;
            
            // If spreading into a rest parameter at the end, pass array directly
            if (calleeHasRest && argTypes.size() >= expectedParamTypes.size() - 1) {
                // We're at or past the rest param position - pass array directly
                argTypes.push_back(spreadType);
            } else if (remainingParams > 0) {
                // Push element type for each remaining fixed parameter
                for (size_t j = 0; j < remainingParams; j++) {
                    argTypes.push_back(elementType);
                }
            } else {
                // If no known parameter count, push as array (fallback for varargs/rest)
                argTypes.push_back(spreadType);
            }
            continue;
        }
        
        // Check if this is an arrow function that needs contextual typing
        bool isArrowOrFn = (arg->getKind() == "ArrowFunction" || arg->getKind() == "FunctionExpression");
        if (isArrowOrFn) {
            // Try to get expected callback type
            std::shared_ptr<FunctionType> expectedCb = nullptr;
            
            // First try known API patterns
            expectedCb = getExpectedCallbackType(objName, methodName, i);
            
            // Then try from callee's function type
            if (!expectedCb && i < expectedParamTypes.size() && expectedParamTypes[i]->kind == TypeKind::Function) {
                expectedCb = std::static_pointer_cast<FunctionType>(expectedParamTypes[i]);
            }
            
            if (expectedCb) {
                pushContextualType(expectedCb);
                visit(arg.get());
                popContextualType();
                argTypes.push_back(lastType);
                continue;
            }
        }
        
        visit(arg.get());
        argTypes.push_back(lastType);
    }

    // Resolve type arguments if any
    std::vector<std::shared_ptr<Type>> resolvedTypeArguments;
    for (const auto& typeName : node->typeArguments) {
        resolvedTypeArguments.push_back(parseType(typeName, symbols));
    }
    node->resolvedTypeArguments = resolvedTypeArguments;

    // Re-visit callee (already done above, but calleeType might need it)
    // calleeType already set above

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
             node->inferredType = lastType;
             return;
        } else if (prop->name == "get") {
             // Check if this is a Map.get() call
             visit(prop->expression.get());
             if (lastType && lastType->kind == TypeKind::Map) {
                 auto mapType = std::dynamic_pointer_cast<MapType>(lastType);
                 if (mapType && mapType->valueType) {
                     lastType = mapType->valueType;
                 } else {
                     lastType = std::make_shared<Type>(TypeKind::Any);
                 }
             } else {
                 // Default for other get calls
                 lastType = std::make_shared<Type>(TypeKind::Int);
             }
             node->inferredType = lastType;
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
            // Update node with inferred type arguments so IRGenerator can use them
            node->resolvedTypeArguments = resolvedTypeArguments;
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
        node->inferredType = lastType;  // CRITICAL: Set inferredType for function variable calls
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
        std::string argTypesStr;
        for (const auto& t : argTypes) {
            if (!argTypesStr.empty()) argTypesStr += ", ";
            argTypesStr += t->toString();
        }
        SPDLOG_INFO("Recording function usage for {}: argTypes=[{}]", calleeName, argTypesStr);
        functionUsages[calleeName].push_back({argTypes, resolvedTypeArguments});
    }
    
    // If we haven't determined a more specific type, default to Any
    if (!lastType) {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
}

void Analyzer::visitNewExpression(ast::NewExpression* node) {
    if (currentModule && currentModule->type == ModuleType::UntypedJavaScript) {
        // Skip type checking for JS
        visit(node->expression.get());
        for (auto& arg : node->arguments) {
            visit(arg.get());
        }
        lastType = std::make_shared<Type>(TypeKind::Any);
        node->inferredType = lastType;
        return;
    }

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
    
    // Handle new net.SocketAddress() or new net.BlockList() - property access expression
    if (auto propAccess = dynamic_cast<PropertyAccessExpression*>(node->expression.get())) {
        // The expression should have been visited and we have the constructor type
        // Check if lastType is already a ClassType (e.g., new http.Agent())
        if (lastType && lastType->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(lastType);
            if (classType->isAbstract) {
                reportError(fmt::format("Cannot create an instance of an abstract class '{}'", classType->name));
            }
            node->inferredType = lastType;
            return;
        }
        // Check if lastType is a constructor function that returns a class
        if (lastType && lastType->kind == TypeKind::Object) {
            auto objType = std::static_pointer_cast<ObjectType>(lastType);
            if (objType->fields.count("new")) {
                auto newMethod = objType->fields["new"];
                if (newMethod->kind == TypeKind::Function) {
                    auto funcType = std::static_pointer_cast<FunctionType>(newMethod);
                    if (funcType->returnType) {
                        lastType = funcType->returnType;
                        node->inferredType = lastType;
                        return;
                    }
                }
            }
        }
    }
    
    if (auto id = dynamic_cast<Identifier*>(node->expression.get())) {
        if (id->name == "Map") {
            // Create MapType with type arguments if available
            if (resolvedTypeArguments.size() >= 2) {
                lastType = std::make_shared<MapType>(resolvedTypeArguments[0], resolvedTypeArguments[1]);
            } else {
                lastType = std::make_shared<MapType>(); // Defaults to Map<any, any>
            }
            node->inferredType = lastType;
            return;
        } else if (id->name == "Set") {
            // Create SetTypeStruct with type argument if available
            if (resolvedTypeArguments.size() >= 1) {
                lastType = std::make_shared<SetTypeStruct>(resolvedTypeArguments[0]);
            } else {
                lastType = std::make_shared<SetTypeStruct>(); // Defaults to Set<any>
            }
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

std::vector<std::shared_ptr<Type>> Analyzer::inferTypeArguments(
    const std::vector<std::shared_ptr<TypeParameterType>>& typeParams,
    const std::vector<std::shared_ptr<Type>>& paramTypes,
    const std::vector<std::shared_ptr<Type>>& argTypes) {
    
    std::map<std::string, std::shared_ptr<Type>> inferred;
    
    auto inferFromTypes = [&](auto self, std::shared_ptr<Type> paramType, std::shared_ptr<Type> argType) -> void {
        if (!paramType || !argType) return;

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
        } else if (paramType->kind == TypeKind::Array && argType->kind == TypeKind::Tuple) {
            // Handle Array vs Tuple: [1,2,3] has type Tuple but should match T[]
            auto pa = std::static_pointer_cast<ArrayType>(paramType);
            auto ta = std::static_pointer_cast<TupleType>(argType);
            if (!ta->elementTypes.empty()) {
                // Use the first element type to infer T
                self(self, pa->elementType, ta->elementTypes[0]);
            }
        } else if (paramType->kind == TypeKind::Function && argType->kind == TypeKind::Function) {
            // Handle function types for callbacks like (x: T) => U
            auto pf = std::static_pointer_cast<FunctionType>(paramType);
            auto af = std::static_pointer_cast<FunctionType>(argType);
            // Match parameter types
            size_t pcount = std::min(pf->paramTypes.size(), af->paramTypes.size());
            for (size_t i = 0; i < pcount; ++i) {
                self(self, pf->paramTypes[i], af->paramTypes[i]);
            }
            // Match return type
            if (pf->returnType && af->returnType) {
                self(self, pf->returnType, af->returnType);
            }
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


