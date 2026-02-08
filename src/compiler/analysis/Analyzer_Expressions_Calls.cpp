#include "Analyzer.h"
#include "Monomorphizer.h"
#include "../ast/AstNodes.h"
#include <fmt/core.h>
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/spdlog.h>
#include <iostream>

namespace ts {
using namespace ast;

static bool paramHasDefaultInitializer(const std::shared_ptr<FunctionType>& funcType, size_t paramIndex) {
    if (!funcType || !funcType->node) return false;

    if (auto fn = dynamic_cast<ast::FunctionDeclaration*>(funcType->node)) {
        if (paramIndex >= fn->parameters.size()) return false;
        const auto& p = fn->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    if (auto fnExpr = dynamic_cast<ast::FunctionExpression*>(funcType->node)) {
        if (paramIndex >= fnExpr->parameters.size()) return false;
        const auto& p = fnExpr->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    if (auto arrow = dynamic_cast<ast::ArrowFunction*>(funcType->node)) {
        if (paramIndex >= arrow->parameters.size()) return false;
        const auto& p = arrow->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    if (auto method = dynamic_cast<ast::MethodDefinition*>(funcType->node)) {
        if (paramIndex >= method->parameters.size()) return false;
        const auto& p = method->parameters[paramIndex];
        return p && p->initializer && !p->isRest;
    }

    return false;
}

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
            std::string modPath = currentModule ? currentModule->path : "";
            functionUsages[calleeName].push_back({argTypes, {}, modPath});

            if (calleeName == "require" && !node->arguments.empty()) {
                if (auto lit = dynamic_cast<ast::StringLiteral*>(node->arguments[0].get())) {
                    loadModule(lit->value);
                }
            }
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
        if (methodName == "require" && !node->arguments.empty()) {
            if (auto lit = dynamic_cast<ast::StringLiteral*>(node->arguments[0].get())) {
                loadModule(lit->value);
            }
        }
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
            // Try to get expected callback type from multiple sources
            std::shared_ptr<FunctionType> expectedCb = nullptr;

            // 1. Try from callee's registered function type params (includes extension-registered types).
            //    This is the primary path — extensions define full method signatures with
            //    function-typed parameters that carry callback param types.
            if (!expectedParamTypes.empty()) {
                // Exact index match
                if (i < expectedParamTypes.size() && expectedParamTypes[i] &&
                    expectedParamTypes[i]->kind == TypeKind::Function) {
                    auto funcParam = std::static_pointer_cast<FunctionType>(expectedParamTypes[i]);
                    if (!funcParam->paramTypes.empty()) {
                        expectedCb = funcParam;
                    }
                }
                // If exact index has no function type, scan for the nearest function-typed
                // param at or after index i. This handles optional params before the callback
                // (e.g., http.createServer has optional 'options' at index 0, callback at index 1,
                // but callers typically pass the callback as arg 0).
                if (!expectedCb) {
                    for (size_t j = i; j < expectedParamTypes.size(); j++) {
                        if (expectedParamTypes[j] && expectedParamTypes[j]->kind == TypeKind::Function) {
                            auto funcParam = std::static_pointer_cast<FunctionType>(expectedParamTypes[j]);
                            if (!funcParam->paramTypes.empty()) {
                                expectedCb = funcParam;
                                break;
                            }
                        }
                    }
                }
            }

            // 2. Fallback: hardcoded API patterns (legacy — will be removed once all
            //    extensions carry full callback signatures in their method definitions).
            if (!expectedCb) {
                expectedCb = getExpectedCallbackType(objName, methodName, i);
            }

            // 3. Check for Promise.then() / Promise.catch() contextual typing
            if (!expectedCb) {
                if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
                    // Get the type of the object (the promise)
                    visit(prop->expression.get());
                    auto promiseType = lastType;

                    if (promiseType && promiseType->kind == TypeKind::Class) {
                        auto cls = std::static_pointer_cast<ClassType>(promiseType);
                        if (cls->name.find("Promise") == 0 && !cls->typeArguments.empty()) {
                            auto valueType = cls->typeArguments[0];

                            // promise.then(onFulfilled, onRejected)
                            if (methodName == "then") {
                                expectedCb = std::make_shared<FunctionType>();
                                expectedCb->paramTypes.push_back(valueType);  // value: T
                                expectedCb->returnType = std::make_shared<Type>(TypeKind::Any);
                            }
                            // promise.catch(onRejected)
                            else if (methodName == "catch" && i == 0) {
                                expectedCb = std::make_shared<FunctionType>();
                                expectedCb->paramTypes.push_back(valueType);  // error: T
                                expectedCb->returnType = std::make_shared<Type>(TypeKind::Any);
                            }
                        }
                    }
                }
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

    // If an argument is explicitly `undefined` and the callee has a default initializer,
    // treat the argument type as the declared parameter type so specialization ABI stays consistent.
    if (calleeType && calleeType->kind == TypeKind::Function) {
        auto funcType = std::static_pointer_cast<FunctionType>(calleeType);
        for (size_t i = 0; i < argTypes.size() && i < funcType->paramTypes.size(); ++i) {
            if (!argTypes[i]) continue;
            if (argTypes[i]->kind != TypeKind::Undefined && argTypes[i]->kind != TypeKind::Void) continue;
            if (!paramHasDefaultInitializer(funcType, i)) continue;
            if (!funcType->paramTypes[i]) continue;
            argTypes[i] = funcType->paramTypes[i];
        }
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
        } else if (prop->name == "indexOf" || prop->name == "lastIndexOf") {
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "toLowerCase") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "toUpperCase") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "normalize") {
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
        } else if (prop->name == "sort" || prop->name == "reverse") {
             // sort() and reverse() return the same array (for chaining)
             visit(prop->expression.get());
             // lastType now holds the array type
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
        } else if (prop->name == "reduce" || prop->name == "reduceRight") {
             // For now, assume reduce returns the same type as the array elements or initial value
             lastType = std::make_shared<Type>(TypeKind::Any);
             return;
        } else if (prop->name == "some" || prop->name == "every") {
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             return;
        } else if (prop->name == "find" || prop->name == "findLast") {
             visit(prop->expression.get());
             if (lastType->kind == TypeKind::Array) {
                 lastType = std::static_pointer_cast<ArrayType>(lastType)->elementType;
             }
             return;
        } else if (prop->name == "findIndex" || prop->name == "findLastIndex") {
             lastType = std::make_shared<Type>(TypeKind::Int);
             return;
        } else if (prop->name == "flat" || prop->name == "flatMap") {
             visit(prop->expression.get());
             return;
        } else if (prop->name == "slice") {
             visit(prop->expression.get());
             // lastType is now the type of the array, which is what slice returns
             return;
        } else if (prop->name == "fill") {
             visit(prop->expression.get());
             // fill() returns the modified array
             return;
        } else if (prop->name == "join") {
             lastType = std::make_shared<Type>(TypeKind::String);
             return;
        } else if (prop->name == "set") {
             lastType = std::make_shared<Type>(TypeKind::Void);
             node->inferredType = lastType;
             return;
        } else if (prop->name == "get") {
             // Check if this is a Map.get() or WeakMap.get() call
             visit(prop->expression.get());
             if (lastType && lastType->kind == TypeKind::Map) {
                 auto mapType = std::dynamic_pointer_cast<MapType>(lastType);
                 if (mapType && mapType->valueType) {
                     lastType = mapType->valueType;
                 } else {
                     lastType = std::make_shared<Type>(TypeKind::Any);
                 }
                 node->inferredType = lastType;
                 return;
             } else if (lastType && lastType->kind == TypeKind::Class) {
                 // WeakMap.get() or other class with get method - don't handle here
                 // Let the class method resolution below handle it
                 // Reset lastType so we can re-visit the expression below
             } else {
                 // Default for other get calls (e.g., array element access)
                 lastType = std::make_shared<Type>(TypeKind::Int);
                 node->inferredType = lastType;
                 return;
             }
        } else if (prop->name == "has") {
             lastType = std::make_shared<Type>(TypeKind::Boolean);
             node->inferredType = lastType;
             return;
        }

        // Check for Math methods
        if (auto obj = dynamic_cast<Identifier*>(prop->expression.get())) {
            if (obj->name == "Math") {
                if (prop->name == "min" || prop->name == "max" || prop->name == "abs" || prop->name == "floor" || prop->name == "ceil" || prop->name == "round" || prop->name == "clz32" || prop->name == "imul") {
                    lastType = std::make_shared<Type>(TypeKind::Int);
                    node->inferredType = lastType;
                    return;
                } else if (prop->name == "sqrt" || prop->name == "pow" || prop->name == "random" ||
                           prop->name == "log10" || prop->name == "log2" || prop->name == "log1p" ||
                           prop->name == "expm1" || prop->name == "cosh" || prop->name == "sinh" ||
                           prop->name == "tanh" || prop->name == "acosh" || prop->name == "asinh" ||
                           prop->name == "atanh" || prop->name == "cbrt" || prop->name == "hypot" ||
                           prop->name == "trunc" || prop->name == "fround") {
                    lastType = std::make_shared<Type>(TypeKind::Double);
                    node->inferredType = lastType;
                    return;
                }
            } else if (obj->name == "JSON") {
                if (prop->name == "parse") {
                    lastType = std::make_shared<Type>(TypeKind::Any);
                    node->inferredType = lastType;
                    return;
                } else if (prop->name == "stringify") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    node->inferredType = lastType;
                    return;
                }
            } else if (obj->name == "process") {
                if (prop->name == "cwd") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    node->inferredType = lastType;
                    return;
                } else if (prop->name == "exit") {
                    lastType = std::make_shared<Type>(TypeKind::Void);
                    node->inferredType = lastType;
                    return;
                }
            } else if (obj->name == "crypto") {
                if (prop->name == "md5") {
                    lastType = std::make_shared<Type>(TypeKind::String);
                    node->inferredType = lastType;
                    return;
                }
            } else if (obj->name == "Object") {
                // Object.assign(target, ...sources) returns the type of target
                if (prop->name == "assign" && node->arguments.size() > 0) {
                    visit(node->arguments[0].get());
                    // Return the type of the first argument (target)
                    node->inferredType = lastType;
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
                // For AOT: use implementation's return type, not overload signature's.
                // The implementation (with body) has the actual runtime return type.
                // Overload signatures may declare specific types like 'string', but the
                // implementation typically returns 'any' which is TsValue* at runtime.
                if (methodType && cls->methods.count(prop->name)) {
                    auto implType = cls->methods[prop->name];
                    methodType = std::make_shared<FunctionType>(*methodType);
                    methodType->returnType = implType->returnType;
                }
            } else if (cls->methods.count(prop->name)) {
                methodType = cls->methods[prop->name];
            }

            // Also check static methods (e.g., Buffer.from(), Buffer.alloc(), Date.now())
            // When a ClassType is used as a value (not instantiated with new), method calls
            // should resolve to static methods if no instance method matches.
            if (!methodType) {
                if (cls->staticMethodOverloads.count(prop->name)) {
                    methodType = resolveOverload(cls->staticMethodOverloads[prop->name], argTypes);
                    if (methodType && cls->staticMethods.count(prop->name)) {
                        auto implType = cls->staticMethods[prop->name];
                        methodType = std::make_shared<FunctionType>(*methodType);
                        methodType->returnType = implType->returnType;
                    }
                } else if (cls->staticMethods.count(prop->name)) {
                    methodType = cls->staticMethods[prop->name];
                }
            }

            if (methodType) {
                lastType = methodType->returnType;
                node->inferredType = lastType;  // CRITICAL: Set inferredType before return

                return;
            }
        } else if (objType->kind == TypeKind::Interface) {
            auto inter = std::static_pointer_cast<InterfaceType>(objType);
            std::shared_ptr<FunctionType> methodType = nullptr;
            
            if (inter->methodOverloads.count(prop->name)) {
                methodType = resolveOverload(inter->methodOverloads[prop->name], argTypes);
                // For AOT: use implementation's return type, not overload signature's
                if (methodType && inter->methods.count(prop->name)) {
                    auto implType = inter->methods[prop->name];
                    methodType = std::make_shared<FunctionType>(*methodType);
                    methodType->returnType = implType->returnType;
                }
            } else if (inter->methods.count(prop->name)) {
                methodType = inter->methods[prop->name];
            }
            
            if (methodType) {
                lastType = methodType->returnType;
                node->inferredType = lastType;  // CRITICAL: Set inferredType before return
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
                    // For AOT: use implementation's return type, not overload signature's
                    if (methodType && cls->staticMethods.count(prop->name)) {
                        auto implType = cls->staticMethods[prop->name];
                        methodType = std::make_shared<FunctionType>(*methodType);
                        methodType->returnType = implType->returnType;
                    }
                } else if (cls->staticMethods.count(prop->name)) {
                    methodType = cls->staticMethods[prop->name];
                }
                
                if (methodType) {
                    // Special handling for Promise.resolve and Promise.reject - infer generic type from argument
                    if (cls->name.find("Promise") == 0 && (prop->name == "resolve" || prop->name == "reject")) {
                        if (!argTypes.empty() && argTypes[0]->kind != TypeKind::Undefined) {
                            // Promise.resolve(value) -> Promise<typeof value>
                            auto promiseType = std::make_shared<ClassType>("Promise");
                            promiseType->typeArguments.push_back(argTypes[0]);
                            lastType = promiseType;
                            return;
                        }
                    }
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
            // Update node with inferred type arguments so codegen can use them
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
        
        std::string modPath = currentModule ? currentModule->path : "";
        if (auto id = dynamic_cast<Identifier*>(node->callee.get())) {
            functionUsages[id->name].push_back({argTypes, resolvedTypeArguments, modPath});
        } else if (auto prop = dynamic_cast<PropertyAccessExpression*>(node->callee.get())) {
            if (prop->expression->inferredType && prop->expression->inferredType->kind == TypeKind::Namespace) {
                functionUsages[prop->name].push_back({argTypes, resolvedTypeArguments, modPath});
            }
        }
        node->inferredType = lastType;  // CRITICAL: Set inferredType for function variable calls
        return;
    }

    // Handle callable interfaces (hybrid types) - interfaces with call signatures
    if (calleeType->kind == TypeKind::Interface) {
        auto inter = std::static_pointer_cast<InterfaceType>(calleeType);
        if (inter->isCallable()) {
            auto callSig = inter->getCallSignature();
            if (callSig) {
                lastType = callSig->returnType ? callSig->returnType : std::make_shared<Type>(TypeKind::Void);
                node->inferredType = lastType;
                return;
            }
        }
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
        std::string modPath = currentModule ? currentModule->path : "";
        functionUsages[calleeName].push_back({argTypes, resolvedTypeArguments, modPath});
    }

    // If the callee is Any type, the return type must also be Any
    // This is critical for dynamic function calls like promisified functions
    if (calleeType && calleeType->kind == TypeKind::Any) {
        lastType = std::make_shared<Type>(TypeKind::Any);
        node->inferredType = lastType;
        return;
    }

    // If we haven't determined a more specific type, default to Any
    if (!lastType) {
        lastType = std::make_shared<Type>(TypeKind::Any);
    }
    node->inferredType = lastType;
}

// Helper: Infer Promise<T> type from resolve() calls in executor body
std::shared_ptr<Type> Analyzer::inferPromiseTypeFromExecutor(ast::Node* body, const std::string& resolveParamName) {
    if (!body) return nullptr;

    // Handle block statement (arrow function with braces)
    if (auto block = dynamic_cast<BlockStatement*>(body)) {
        return inferPromiseTypeFromFunctionBody(block->statements, resolveParamName);
    }

    // Handle expression body (arrow function without braces: resolve => resolve(100))
    if (auto expr = dynamic_cast<Expression*>(body)) {
        // Check if this is a call to resolve
        if (auto callExpr = dynamic_cast<CallExpression*>(expr)) {
            if (auto callee = dynamic_cast<Identifier*>(callExpr->callee.get())) {
                if (callee->name == resolveParamName && !callExpr->arguments.empty()) {
                    // Found resolve(arg) - return the type of the first argument
                    if (callExpr->arguments[0]->inferredType) {
                        return callExpr->arguments[0]->inferredType;
                    }
                }
            }
        }
    }

    return nullptr;
}

// Helper: Scan function body statements for resolve() calls
std::shared_ptr<Type> Analyzer::inferPromiseTypeFromFunctionBody(
    const std::vector<ast::StmtPtr>& statements,
    const std::string& resolveParamName) {

    for (const auto& stmt : statements) {
        // Check expression statements for calls
        if (auto exprStmt = dynamic_cast<ExpressionStatement*>(stmt.get())) {
            if (auto callExpr = dynamic_cast<CallExpression*>(exprStmt->expression.get())) {
                if (auto callee = dynamic_cast<Identifier*>(callExpr->callee.get())) {
                    if (callee->name == resolveParamName && !callExpr->arguments.empty()) {
                        // Found resolve(arg) - return the type of the first argument
                        if (callExpr->arguments[0]->inferredType) {
                            return callExpr->arguments[0]->inferredType;
                        }
                    }
                }
            }
        }
    }

    return nullptr;
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

    // Check if this is Promise constructor before visiting arguments
    std::string constructorName;
    if (auto id = dynamic_cast<Identifier*>(node->expression.get())) {
        constructorName = id->name;
    }

    std::vector<std::shared_ptr<Type>> ctorArgTypes;
    for (size_t i = 0; i < node->arguments.size(); ++i) {
        auto& arg = node->arguments[i];

        // Provide contextual typing for Promise executor
        if (constructorName == "Promise" && i == 0) {
            // Executor type: (resolve: (value: any) => void, reject: (reason: any) => void) => void
            // We use 'any' initially; later we'll infer the actual type from usage
            auto resolveParam = std::make_shared<FunctionType>();
            resolveParam->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            resolveParam->returnType = std::make_shared<Type>(TypeKind::Void);

            auto rejectParam = std::make_shared<FunctionType>();
            rejectParam->paramTypes.push_back(std::make_shared<Type>(TypeKind::Any));
            rejectParam->returnType = std::make_shared<Type>(TypeKind::Void);

            auto executorType = std::make_shared<FunctionType>();
            executorType->paramTypes.push_back(resolveParam);
            executorType->paramTypes.push_back(rejectParam);
            executorType->returnType = std::make_shared<Type>(TypeKind::Void);

            pushContextualType(executorType);
            visit(arg.get());
            popContextualType();
            ctorArgTypes.push_back(lastType);
        } else {
            visit(arg.get());
            ctorArgTypes.push_back(lastType);
        }
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

            // Special handling for Promise constructor: infer T from executor callback
            if (classType->name == "Promise" && resolvedTypeArguments.empty() && !classType->typeParameters.empty() && !ctorArgTypes.empty()) {
                std::shared_ptr<Type> inferredT;

                // Get the executor argument (should be an arrow function or function expression)
                if (!node->arguments.empty()) {
                    auto executorArg = node->arguments[0].get();

                    // Try to extract type from executor body
                    if (auto arrowFunc = dynamic_cast<ArrowFunction*>(executorArg)) {
                        // Scan the body for calls to the first parameter (resolve)
                        if (!arrowFunc->parameters.empty()) {
                            auto paramName = arrowFunc->parameters[0]->name.get();
                            if (auto paramId = dynamic_cast<Identifier*>(paramName)) {
                                std::string resolveParamName = paramId->name;
                                inferredT = inferPromiseTypeFromExecutor(arrowFunc->body.get(), resolveParamName);
                            }
                        }
                    } else if (auto funcExpr = dynamic_cast<FunctionExpression*>(executorArg)) {
                        if (!funcExpr->parameters.empty()) {
                            auto paramName = funcExpr->parameters[0]->name.get();
                            if (auto paramId = dynamic_cast<Identifier*>(paramName)) {
                                std::string resolveParamName = paramId->name;
                                inferredT = inferPromiseTypeFromFunctionBody(funcExpr->body, resolveParamName);
                            }
                        }
                    }
                }

                // If we inferred a type, use it; otherwise default to any
                if (inferredT) {
                    auto instantiated = std::make_shared<ClassType>(classType->name);
                    instantiated->typeArguments.push_back(inferredT);
                    instantiated->typeParameters = classType->typeParameters;
                    instantiated->methods = classType->methods;
                    instantiated->fields = classType->fields;
                    instantiated->staticMethods = classType->staticMethods;
                    lastType = instantiated;
                    node->inferredType = lastType;
                    classUsages[id->name].push_back({inferredT});
                    return;
                }
            }

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
        // Check if the symbol is a class type (from class expression assigned to variable)
        if (sym && sym->type->kind == TypeKind::Class) {
            auto classType = std::static_pointer_cast<ClassType>(sym->type);
            if (classType->isAbstract) {
                reportError(fmt::format("Cannot create an instance of an abstract class '{}'", classType->name));
            }
            lastType = classType;
            node->inferredType = lastType;
            classUsages[classType->name].push_back(resolvedTypeArguments);
            return;
        }
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

                // Apply resolved type arguments to generic classes
                if (!resolvedTypeArguments.empty()) {
                    auto instantiated = std::make_shared<ClassType>(classType->name);
                    instantiated->typeArguments = resolvedTypeArguments;
                    // Copy other relevant fields
                    instantiated->methods = classType->methods;
                    instantiated->fields = classType->fields;
                    instantiated->staticMethods = classType->staticMethods;
                    lastType = instantiated;
                } else {
                    lastType = classType;
                }
                node->inferredType = lastType;
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
            // Fallback order: defaultType > constraint > Any
            if (tp->defaultType) {
                result.push_back(tp->defaultType);
            } else if (tp->constraint) {
                result.push_back(tp->constraint);
            } else {
                result.push_back(std::make_shared<Type>(TypeKind::Any));
            }
        }
    }
    return result;
}

} // namespace ts

