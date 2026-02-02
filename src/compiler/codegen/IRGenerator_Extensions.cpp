// IRGenerator_Extensions.cpp - Extension contract codegen integration
//
// Handles method calls and property access on extension-defined types.
// Extension contracts define types in JSON with lowering specifications
// that map directly to LLVM IR types.

#include "IRGenerator.h"
#include "../extensions/ExtensionLoader.h"
#include <spdlog/spdlog.h>

namespace ts {

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
                    // Unbox the value to get integer
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
                    // Box the value
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

bool IRGenerator::tryGenerateExtensionPropertyAccess(ast::PropertyAccessExpression* prop) {
    // Check if the receiver type is a Class
    if (!prop->expression->inferredType || prop->expression->inferredType->kind != TypeKind::Class) {
        return false;
    }

    auto classType = std::static_pointer_cast<ClassType>(prop->expression->inferredType);
    const std::string& className = classType->name;
    const std::string& propName = prop->name;

    // Check if this is an extension-defined type
    auto& registry = ext::ExtensionRegistry::instance();
    if (!registry.isExtensionType(className)) {
        return false;
    }

    // Look up the property in the extension registry
    const ext::PropertyDefinition* propDef = registry.findProperty(className, propName);
    if (!propDef) {
        SPDLOG_DEBUG("Extension type {} has no property {}", className, propName);
        return false;
    }

    // Check if property has a getter function
    if (!propDef->getter.has_value()) {
        SPDLOG_DEBUG("Extension property {}.{} has no getter", className, propName);
        return false;
    }

    SPDLOG_INFO("Generating extension property access: {}.{} -> {}", className, propName, propDef->getter.value());

    // Evaluate the receiver (this) argument
    visit(prop->expression.get());
    llvm::Value* thisArg = lastValue;
    emitNullCheckForExpression(prop->expression.get(), thisArg);

    // Build LLVM function type for getter
    // Getter takes self and returns the property value
    llvm::Type* returnType = builder->getPtrTy();  // Default to pointer (boxed)
    bool returnsBoxed = true;  // Default to boxed

    if (propDef->lowering.has_value()) {
        const auto& lowering = propDef->lowering.value();
        returnType = getLLVMTypeFromLowering(lowering.returns, *builder);
        returnsBoxed = (lowering.returns == ext::LoweringType::Boxed);
    }

    llvm::FunctionType* ft = llvm::FunctionType::get(returnType, { builder->getPtrTy() }, false);
    llvm::FunctionCallee fn = module->getOrInsertFunction(propDef->getter.value(), ft);

    // Get self pointer
    llvm::Value* selfArg = thisArg;
    if (!selfArg->getType()->isPointerTy()) {
        selfArg = builder->CreateIntToPtr(selfArg, builder->getPtrTy());
    }

    // Generate the call
    lastValue = createCall(ft, fn.getCallee(), { selfArg });

    // Handle return value boxing
    if (returnsBoxed) {
        boxedValues.insert(lastValue);
    }

    return true;
}

} // namespace ts
