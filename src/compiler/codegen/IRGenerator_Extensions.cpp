// IRGenerator_Extensions.cpp - Extension contract codegen integration
//
// Handles method calls and property access on extension-defined types and objects.
// Extension contracts define types in JSON with lowering specifications
// that map directly to LLVM IR types.

#include "IRGenerator.h"
#include "BoxingPolicy.h"
#include "../extensions/ExtensionLoader.h"
#include "../extensions/ExtensionSchema.h"
#include <spdlog/spdlog.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

namespace ts {
using namespace ast;

// Convert extension LoweringType to LLVM Type
static llvm::Type* getLLVMTypeFromLowering(ext::LoweringType lt, llvm::IRBuilder<>& builder) {
    switch (lt) {
        case ext::LoweringType::Ptr:
            return builder.getPtrTy();
        case ext::LoweringType::I64:
            return builder.getInt64Ty();
        case ext::LoweringType::I32:
            return builder.getInt32Ty();
        case ext::LoweringType::F64:
            return builder.getDoubleTy();
        case ext::LoweringType::I1:
            return builder.getInt1Ty();
        case ext::LoweringType::Boxed:
            return builder.getPtrTy();  // TsValue*
        case ext::LoweringType::Void:
            return builder.getVoidTy();
    }
    return builder.getPtrTy();  // Default to pointer
}

// Register boxing info for an extension method
static void registerExtMethodBoxing(BoxingPolicy& bp, const std::string& fnName, const ext::MethodDefinition& method) {
    std::vector<bool> argBoxing;
    bool returnsBoxed = false;

    if (method.lowering) {
        for (auto lt : method.lowering->args) {
            argBoxing.push_back(lt == ext::LoweringType::Boxed);
        }
        returnsBoxed = (method.lowering->returns == ext::LoweringType::Boxed);
    } else {
        for (size_t i = 0; i < method.params.size(); ++i) {
            argBoxing.push_back(true);
        }
        returnsBoxed = true;
    }

    bp.registerRuntimeApi(fnName, argBoxing, returnsBoxed);
}

//=============================================================================
// Object/Namespace Method Calls (e.g., path.join(), path.win32.join())
//=============================================================================

bool IRGenerator::tryGenerateExtensionCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    auto& extRegistry = ext::ExtensionRegistry::instance();

    // Determine the object name and whether this is a nested object access
    std::string objectName;
    std::string nestedName;
    int platform = 0;  // 0=default, 1=win32, 2=posix

    // Check for pattern: obj.method() or obj.nested.method()
    if (auto id = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
        objectName = id->name;
    } else if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(prop->expression.get())) {
        if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
            objectName = id->name;
            nestedName = innerProp->name;
            // Detect platform for path module
            if (nestedName == "win32") platform = 1;
            else if (nestedName == "posix") platform = 2;
        }
    }

    if (objectName.empty()) return false;

    // Look up the method in the extension registry
    const ext::MethodDefinition* methodDef = nullptr;

    if (!nestedName.empty()) {
        methodDef = extRegistry.findNestedObjectMethod(objectName, nestedName, prop->name);
    } else {
        methodDef = extRegistry.findObjectMethod(objectName, prop->name);
    }

    if (!methodDef) return false;

    SPDLOG_DEBUG("Extension call: {}.{}{} -> {}",
                 objectName,
                 nestedName.empty() ? "" : (nestedName + "."),
                 prop->name,
                 methodDef->call);

    // Determine the runtime function name
    std::string fnName = methodDef->call;

    // If method has platformArg, use _ex variant with platform arg for nested calls
    bool hasPlatformArg = methodDef->platformArg.has_value();
    if (hasPlatformArg && platform != 0) {
        // For nested objects (win32/posix), use the _ex variant with platform arg
        if (fnName.find("_ex") == std::string::npos) {
            fnName += "_ex";
        }
    }

    // Register boxing info
    registerExtMethodBoxing(boxingPolicy, fnName, *methodDef);

    // Build LLVM function type from lowering spec
    std::vector<llvm::Type*> argTypes;
    llvm::Type* retType = builder->getPtrTy();

    if (methodDef->lowering) {
        for (auto lt : methodDef->lowering->args) {
            argTypes.push_back(getLLVMTypeFromLowering(lt, *builder));
        }
        retType = getLLVMTypeFromLowering(methodDef->lowering->returns, *builder);
        // Note: platform arg type is already included in lowering spec for nested object methods
    } else {
        // Default: all ptr args, ptr return
        for (size_t i = 0; i < methodDef->params.size(); ++i) {
            argTypes.push_back(builder->getPtrTy());
        }
    }

    // Handle special cases for variadic/rest parameters
    bool isVariadic = false;
    for (const auto& param : methodDef->params) {
        if (param.rest) {
            isVariadic = true;
            break;
        }
    }

    // Collect arguments
    std::vector<llvm::Value*> args;

    if (isVariadic) {
        // For variadic functions, pack all args into an array
        std::vector<llvm::Value*> argValues;
        for (auto& arg : node->arguments) {
            visit(arg.get());
            argValues.push_back(boxValue(lastValue, arg->inferredType));
        }

        // Create array
        llvm::FunctionType* arrayCreateFt = llvm::FunctionType::get(
            builder->getPtrTy(),
            { llvm::Type::getInt64Ty(*context) },
            false
        );
        llvm::FunctionCallee arrayCreateFn = getRuntimeFunction("ts_array_create_sized", arrayCreateFt);
        llvm::Value* array = createCall(
            arrayCreateFt,
            arrayCreateFn.getCallee(),
            { llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), argValues.size()) }
        );

        // Populate array
        for (size_t i = 0; i < argValues.size(); ++i) {
            emitInlineArraySet(array, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), i), argValues[i]);
        }

        args.push_back(array);
    } else {
        // Regular arguments
        size_t argIdx = 0;
        for (auto& arg : node->arguments) {
            visit(arg.get());

            bool needsBox = true;
            if (methodDef->lowering && argIdx < methodDef->lowering->args.size()) {
                needsBox = (methodDef->lowering->args[argIdx] == ext::LoweringType::Boxed);
            }

            if (needsBox) {
                args.push_back(boxValue(lastValue, arg->inferredType));
            } else {
                args.push_back(lastValue);
            }
            argIdx++;
        }

        // Add null for optional params not provided
        while (args.size() < methodDef->params.size()) {
            bool isOptional = false;
            if (args.size() < methodDef->params.size()) {
                isOptional = methodDef->params[args.size()].optional;
            }
            if (isOptional) {
                args.push_back(llvm::ConstantPointerNull::get(builder->getPtrTy()));
            } else {
                break;
            }
        }
    }

    // Add platform arg if needed
    if (hasPlatformArg && platform != 0) {
        args.push_back(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), platform));
    }

    // Create function type and call
    llvm::FunctionType* ft = llvm::FunctionType::get(retType, argTypes, false);
    llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
    lastValue = createCall(ft, fn.getCallee(), args);

    // Handle boolean return (isAbsolute returns i32 that should be bool)
    if (methodDef->returns.name == "boolean" && retType == llvm::Type::getInt32Ty(*context)) {
        lastValue = builder->CreateICmpNE(lastValue, builder->getInt32(0));
    }

    return true;
}

//=============================================================================
// Object/Namespace Property Access (e.g., path.sep, path.win32.sep)
//=============================================================================

bool IRGenerator::tryGenerateExtensionPropertyAccess(ast::PropertyAccessExpression* node) {
    auto& extRegistry = ext::ExtensionRegistry::instance();

    // Determine the object name and whether this is a nested object access
    std::string objectName;
    std::string nestedName;
    int platform = 0;

    if (auto id = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        objectName = id->name;
    } else if (auto innerProp = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        if (auto id = dynamic_cast<ast::Identifier*>(innerProp->expression.get())) {
            objectName = id->name;
            nestedName = innerProp->name;
            if (nestedName == "win32") platform = 1;
            else if (nestedName == "posix") platform = 2;
        }
    }

    if (objectName.empty()) return false;

    // Look up the property in the extension registry
    const ext::PropertyDefinition* propDef = nullptr;

    if (!nestedName.empty()) {
        propDef = extRegistry.findNestedObjectProperty(objectName, nestedName, node->name);
    } else {
        propDef = extRegistry.findObjectProperty(objectName, node->name);
    }

    if (!propDef) {
        // Check if it's a nested object access (e.g., path.win32)
        if (extRegistry.findNestedObject(objectName, node->name)) {
            // Return undefined for now - the actual access will be through method calls
            lastValue = getUndefinedValue();
            return true;
        }
        return false;
    }

    SPDLOG_DEBUG("Extension property: {}.{}{} -> {}",
                 objectName,
                 nestedName.empty() ? "" : (nestedName + "."),
                 node->name,
                 propDef->getter.value_or("(no getter)"));

    // Generate getter call if defined
    if (propDef->getter) {
        std::string fnName = *propDef->getter;

        // Handle platform-specific getters
        if (platform == 1 && fnName.find("_win32") == std::string::npos) {
            fnName += "_win32";
        } else if (platform == 2 && fnName.find("_posix") == std::string::npos) {
            fnName += "_posix";
        }

        llvm::Type* retType = builder->getPtrTy();
        if (propDef->lowering) {
            retType = getLLVMTypeFromLowering(propDef->lowering->returns, *builder);
        }

        llvm::FunctionType* ft = llvm::FunctionType::get(retType, {}, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(fnName, ft);
        lastValue = createCall(ft, fn.getCallee(), {});
        return true;
    }

    return false;
}

//=============================================================================
// Instance Method Calls on Extension-Defined Classes
//=============================================================================

bool IRGenerator::tryGenerateExtensionMethodCall(ast::CallExpression* node, ast::PropertyAccessExpression* prop) {
    // Check if the receiver type is a Class
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    const std::string& className = classType->name;
    const std::string& methodName = prop->name;

    // Check if this is an extension-defined type
    auto& registry = ext::ExtensionRegistry::instance();
    if (!registry.isExtensionType(className)) {
        return false;
    }

    // Look up the method in the extension registry
    const ext::MethodDefinition* method = registry.findMethod(className, methodName);
    if (!method) {
        SPDLOG_DEBUG("Extension type {} has no method {}", className, methodName);
        return false;
    }

    SPDLOG_INFO("Generating extension method call: {}.{} -> {}", className, methodName, method->call);

    // Evaluate the receiver (this) argument
    visit(prop->expression.get());
    llvm::Value* thisArg = lastValue;
    emitNullCheckForExpression(prop->expression.get(), thisArg);

    // Build LLVM function type from lowering spec
    std::vector<llvm::Type*> paramTypes;

    // First argument is always 'self' (the receiver) - always a pointer
    paramTypes.push_back(builder->getPtrTy());

    // Add argument types from lowering spec
    if (method->lowering.has_value()) {
        const auto& lowering = method->lowering.value();
        for (const auto& argType : lowering.args) {
            paramTypes.push_back(getLLVMTypeFromLowering(argType, *builder));
        }
    } else {
        // Fallback: use ptr for all args if no lowering spec
        for (size_t i = 0; i < node->arguments.size(); ++i) {
            paramTypes.push_back(builder->getPtrTy());
        }
    }

    // Determine return type from lowering spec
    llvm::Type* returnType = builder->getPtrTy();  // Default to pointer
    bool returnsBoxed = false;
    if (method->lowering.has_value()) {
        const auto& lowering = method->lowering.value();
        returnType = getLLVMTypeFromLowering(lowering.returns, *builder);
        returnsBoxed = (lowering.returns == ext::LoweringType::Boxed);
    }

    // Create the function type
    llvm::FunctionType* ft = llvm::FunctionType::get(returnType, paramTypes, false);

    // Get or declare the runtime function
    llvm::FunctionCallee fn = module->getOrInsertFunction(method->call, ft);

    // Build arguments list
    std::vector<llvm::Value*> args;

    // First arg: self/this
    llvm::Value* selfArg = thisArg;
    if (!selfArg->getType()->isPointerTy()) {
        selfArg = builder->CreateIntToPtr(selfArg, builder->getPtrTy());
    }
    args.push_back(selfArg);

    // Process call arguments
    for (size_t i = 0; i < node->arguments.size(); ++i) {
        visit(node->arguments[i].get());
        llvm::Value* argVal = lastValue;

        // Get expected type from lowering spec
        llvm::Type* expectedType = builder->getPtrTy();  // Default
        if (method->lowering.has_value() && i < method->lowering->args.size()) {
            expectedType = getLLVMTypeFromLowering(method->lowering->args[i], *builder);
        }

        // Cast argument to expected type
        if (argVal->getType() != expectedType) {
            // Handle common conversions
            if (expectedType->isIntegerTy(64)) {
                if (argVal->getType()->isPointerTy()) {
                    argVal = unboxValue(argVal, std::make_shared<Type>(TypeKind::Int));
                } else if (argVal->getType()->isDoubleTy()) {
                    argVal = builder->CreateFPToSI(argVal, builder->getInt64Ty());
                } else if (!argVal->getType()->isIntegerTy(64)) {
                    argVal = castValue(argVal, expectedType);
                }
            } else if (expectedType->isDoubleTy()) {
                if (argVal->getType()->isPointerTy()) {
                    argVal = unboxValue(argVal, std::make_shared<Type>(TypeKind::Double));
                } else if (argVal->getType()->isIntegerTy()) {
                    argVal = builder->CreateSIToFP(argVal, builder->getDoubleTy());
                }
            } else if (expectedType->isPointerTy()) {
                if (!argVal->getType()->isPointerTy()) {
                    argVal = boxValue(argVal, node->arguments[i]->inferredType);
                }
            } else {
                argVal = castValue(argVal, expectedType);
            }
        }

        args.push_back(argVal);
    }

    // Generate the call
    lastValue = createCall(ft, fn.getCallee(), args);

    // Handle return value boxing
    if (returnsBoxed) {
        boxedValues.insert(lastValue);
    }

    // Convert return value to expected TypeScript type if needed
    if (node->inferredType) {
        if (node->inferredType->kind == TypeKind::String && !returnsBoxed) {
            // String return - already a pointer, might need boxing for some consumers
        } else if (node->inferredType->kind == TypeKind::Int && returnType->isPointerTy()) {
            // Need to unbox
            lastValue = unboxValue(lastValue, node->inferredType);
        }
    }

    return true;
}

} // namespace ts
