#include "ASTToHIR.h"
#include "../extensions/ExtensionLoader.h"
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ts::hir {

//==============================================================================
// Constructor / Entry Point
//==============================================================================

ASTToHIR::ASTToHIR() : builder_(nullptr) {}

std::unique_ptr<HIRModule> ASTToHIR::lower(ast::Program* program, const std::string& moduleName) {
    module_ = std::make_unique<HIRModule>(moduleName);
    builder_ = HIRBuilder(module_.get());

    valueCounter_ = 0;
    blockCounter_ = 0;
    scopes_.clear();
    pushScope();  // Global scope

    // Check if we need a module init function for top-level executable code
    // (VariableDeclarations with initializers, ExpressionStatements, etc.)
    bool needsModuleInit = false;
    for (auto& stmt : program->body) {
        std::string kind = stmt->getKind();
        if (kind == "VariableDeclaration" || kind == "ExpressionStatement") {
            needsModuleInit = true;
            break;
        }
    }

    // Create module initialization function for top-level code
    HIRFunction* moduleInitFunc = nullptr;
    if (needsModuleInit) {
        auto initFunc = std::make_unique<HIRFunction>("__module_init");
        initFunc->returnType = HIRType::makeVoid();
        moduleInitFunc = initFunc.get();
        currentFunction_ = moduleInitFunc;

        // Create entry block
        auto entryBlock = initFunc->createBlock("entry");
        builder_.setInsertPoint(entryBlock);
        currentBlock_ = entryBlock;

        module_->functions.push_back(std::move(initFunc));
    }

    // Visit all statements in the program
    for (auto& stmt : program->body) {
        lowerStatement(stmt.get());
    }

    // Add terminator to module init function if it was created
    if (moduleInitFunc && currentBlock_ && !hasTerminator()) {
        builder_.createReturnVoid();
    }

    popScope();
    return std::move(module_);
}

std::unique_ptr<HIRModule> ASTToHIR::lower(ast::Program* program,
                                           const std::vector<Specialization>& specializations,
                                           const std::string& moduleName) {
    module_ = std::make_unique<HIRModule>(moduleName);
    builder_ = HIRBuilder(module_.get());

    // Store specializations for lookup during call generation
    specializations_ = &specializations;

    valueCounter_ = 0;
    blockCounter_ = 0;
    scopes_.clear();
    pushScope();  // Global scope

    // First pass: visit all statements in the program to process classes and globals
    // This ensures class definitions and other declarations are available
    for (auto& stmt : program->body) {
        std::string kind = stmt->getKind();
        // Process class declarations, enum declarations, and imports
        // Skip function declarations as they'll be processed via specializations
        if (kind == "ClassDeclaration" || kind == "EnumDeclaration" ||
            kind == "ImportDeclaration" || kind == "ExportDeclaration") {
            lowerStatement(stmt.get());
        }
    }

    // Second pass: generate functions from specializations (like legacy IRGenerator)
    for (const auto& spec : specializations) {
        if (spec.specializedName.find("lambda") != std::string::npos) {
            // Skip lambda specializations - they'll be generated when encountered
            continue;
        }

        // Get the node - could be FunctionDeclaration or MethodDefinition
        if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            // Create HIR function with the specialized name
            auto func = std::make_unique<HIRFunction>(spec.specializedName);
            func->isAsync = funcNode->isAsync;
            func->isGenerator = funcNode->isGenerator;

            // Handle parameters
            for (size_t paramIdx = 0; paramIdx < funcNode->parameters.size(); ++paramIdx) {
                auto& param = funcNode->parameters[paramIdx];
                // Use specialized type from spec.argTypes if available
                std::shared_ptr<HIRType> paramType;
                if (paramIdx < spec.argTypes.size() && spec.argTypes[paramIdx]) {
                    paramType = convertType(spec.argTypes[paramIdx]);
                } else if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                } else {
                    paramType = HIRType::makeAny();
                }

                // Get parameter name
                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                if (param->isRest) {
                    func->hasRestParam = true;
                }

                func->params.push_back({paramName, paramType});
            }

            // Set return type from specialization
            if (spec.returnType) {
                func->returnType = convertType(spec.returnType);
            } else if (!funcNode->returnType.empty()) {
                func->returnType = convertTypeFromString(funcNode->returnType);
            } else {
                func->returnType = HIRType::makeAny();
            }

            // Create entry block and set up for lowering
            auto entryBlock = func->createBlock("entry");
            currentFunction_ = func.get();
            currentBlock_ = entryBlock;
            builder_.setInsertPoint(entryBlock);

            // Push function scope and bind parameters
            pushFunctionScope(func.get());
            func->nextValueId = static_cast<uint32_t>(func->params.size());
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                // Check if this parameter has a default value
                ast::Parameter* astParam = (i < funcNode->parameters.size()) ? funcNode->parameters[i].get() : nullptr;
                if (astParam && astParam->initializer) {
                    // Parameter has a default value - need to check if undefined and use default
                    auto allocaVal = builder_.createAlloca(paramType);

                    // Check if param is undefined using runtime function
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    // Create basic blocks for the conditional
                    auto defaultBB = func->createBlock("default_param");
                    auto usedBB = func->createBlock("use_param");
                    auto mergeBB = func->createBlock("param_merge");

                    // Branch based on undefined check
                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    // Default block - evaluate default expression and store
                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    // Use param block - store the passed parameter value
                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    // Merge block - continue execution
                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    // Register the alloca as the variable
                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    // No default value - just register the parameter directly
                    defineVariable(paramName, paramValue);
                }
            }

            // Lower function body
            for (auto& stmt : funcNode->body) {
                lowerStatement(stmt.get());
                if (builder_.isBlockTerminated()) {
                    break;
                }
            }

            // Add implicit return if needed
            if (!hasTerminator()) {
                if (func->returnType->kind == HIRTypeKind::Void) {
                    builder_.createReturnVoid();
                } else {
                    // Return undefined for non-void functions without explicit return
                    auto undef = builder_.createConstUndefined();
                    builder_.createReturn(undef);
                }
            }

            popScope();
            module_->functions.push_back(std::move(func));
        } else if (auto* methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            // Handle method definitions (similar to above)
            if (methodNode->isAbstract || !methodNode->hasBody) continue;

            auto func = std::make_unique<HIRFunction>(spec.specializedName);
            func->isAsync = methodNode->isAsync;
            func->isGenerator = methodNode->isGenerator;

            // Add 'this' parameter first for instance methods
            if (!methodNode->isStatic) {
                auto thisType = HIRType::makeAny();
                func->params.push_back({"this", thisType});
            }

            // Handle regular parameters
            for (size_t paramIdx = 0; paramIdx < methodNode->parameters.size(); ++paramIdx) {
                auto& param = methodNode->parameters[paramIdx];
                std::shared_ptr<HIRType> paramType;
                if (paramIdx < spec.argTypes.size() && spec.argTypes[paramIdx]) {
                    paramType = convertType(spec.argTypes[paramIdx]);
                } else if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                } else {
                    paramType = HIRType::makeAny();
                }

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                func->params.push_back({paramName, paramType});
            }

            // Set return type
            if (spec.returnType) {
                func->returnType = convertType(spec.returnType);
            } else if (!methodNode->returnType.empty()) {
                func->returnType = convertTypeFromString(methodNode->returnType);
            } else {
                func->returnType = HIRType::makeAny();
            }

            auto entryBlock = func->createBlock("entry");
            currentFunction_ = func.get();
            currentBlock_ = entryBlock;
            builder_.setInsertPoint(entryBlock);

            pushFunctionScope(func.get());
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
                defineVariable(paramName, paramValue);
            }
            func->nextValueId = static_cast<uint32_t>(func->params.size());

            for (auto& stmt : methodNode->body) {
                lowerStatement(stmt.get());
                if (builder_.isBlockTerminated()) {
                    break;
                }
            }

            if (!hasTerminator()) {
                if (func->returnType->kind == HIRTypeKind::Void) {
                    builder_.createReturnVoid();
                } else {
                    auto undef = builder_.createConstUndefined();
                    builder_.createReturn(undef);
                }
            }

            popScope();
            module_->functions.push_back(std::move(func));
        }
    }

    popScope();
    specializations_ = nullptr;  // Clear to avoid dangling pointer
    return std::move(module_);
}

//==============================================================================
// SSA Helpers
//==============================================================================

std::shared_ptr<HIRValue> ASTToHIR::createValue(std::shared_ptr<HIRType> type) {
    // Delegate to builder to ensure we use the same value counter as HIRFunction
    return builder_.createValue(type);
}

HIRBlock* ASTToHIR::createBlock(const std::string& hint) {
    std::ostringstream ss;
    ss << hint << blockCounter_++;
    return currentFunction_->createBlock(ss.str());
}

void ASTToHIR::pushScope() {
    Scope scope;
    scope.isFunctionBoundary = false;
    scope.owningFunction = currentFunction_;
    scopes_.push_back(scope);
}

void ASTToHIR::pushFunctionScope(HIRFunction* func) {
    Scope scope;
    scope.isFunctionBoundary = true;
    scope.owningFunction = func;
    scopes_.push_back(scope);
}

void ASTToHIR::popScope() {
    if (!scopes_.empty()) {
        scopes_.pop_back();
    }
}

void ASTToHIR::defineVariable(const std::string& name, std::shared_ptr<HIRValue> value) {
    if (!scopes_.empty()) {
        VariableInfo info;
        info.value = value;
        info.isAlloca = false;
        info.elemType = nullptr;
        scopes_.back().variables[name] = info;
    }
}

void ASTToHIR::defineVariableAlloca(const std::string& name, std::shared_ptr<HIRValue> allocaPtr,
                                     std::shared_ptr<HIRType> elemType) {
    if (!scopes_.empty()) {
        VariableInfo info;
        info.value = allocaPtr;
        info.isAlloca = true;
        info.elemType = elemType;
        scopes_.back().variables[name] = info;
    }
}

ASTToHIR::VariableInfo* ASTToHIR::lookupVariableInfo(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

std::shared_ptr<HIRValue> ASTToHIR::lookupVariable(const std::string& name) {
    // Legacy method - looks up and emits load if needed
    auto* info = lookupVariableInfo(name);
    if (!info) return nullptr;

    // If this variable is captured by a nested closure, we need to read from the cell
    if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
        // Use cell-based access: ts_closure_get_cell(closure, index) -> ts_cell_get(cell)
        auto type = info->elemType ? info->elemType : HIRType::makeAny();
        return builder_.createLoadCaptureFromClosure(info->closurePtr, info->captureIndex, type);
    }

    if (info->isAlloca && info->elemType) {
        // Emit a load for alloca-stored variables
        return builder_.createLoad(info->elemType, info->value);
    }
    return info->value;
}

bool ASTToHIR::isCapturedVariable(const std::string& name, size_t* outScopeIndex) {
    // Search from innermost to outermost scope
    // A variable is captured only if it's defined in a DIFFERENT function (outer function)
    // We need to track two things:
    // 1. Have we seen our current function's boundary? (the first one we encounter)
    // 2. Have we crossed into an outer function? (a second function boundary)
    bool seenCurrentFunctionBoundary = false;
    bool crossedFunctionBoundary = false;
    size_t scopeIndex = scopes_.size();

    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it, --scopeIndex) {
        // Track function boundaries
        if (it->isFunctionBoundary) {
            if (seenCurrentFunctionBoundary) {
                // This is a second function boundary - we've crossed into an outer function
                crossedFunctionBoundary = true;
            } else {
                // This is the first function boundary (our current function)
                seenCurrentFunctionBoundary = true;
            }
        }

        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            // Found the variable - is it from an outer function?
            if (crossedFunctionBoundary) {
                if (outScopeIndex) *outScopeIndex = scopeIndex - 1;
                return true;
            }
            return false;
        }
    }

    return false;  // Variable not found
}

void ASTToHIR::registerCapture(const std::string& name, std::shared_ptr<HIRType> type, size_t scopeIndex) {
    // Check if already registered
    for (const auto& cap : pendingCaptures_) {
        if (cap.name == name) return;  // Already captured
    }

    CaptureInfo info;
    info.name = name;
    info.type = type;
    info.outerScopeIndex = scopeIndex;
    pendingCaptures_.push_back(info);
}

//==============================================================================
// Control Flow Helpers
//==============================================================================

void ASTToHIR::emitBranchIfNeeded(HIRBlock* target) {
    if (!hasTerminator()) {
        builder_.createBranch(target);
    }
}

bool ASTToHIR::hasTerminator() {
    HIRBlock* block = builder_.getInsertBlock();
    if (!block || block->instructions.empty()) {
        return false;
    }
    auto& last = block->instructions.back();
    // Check if last instruction is a terminator
    auto op = last->opcode;
    return op == HIROpcode::Branch || op == HIROpcode::CondBranch ||
           op == HIROpcode::Return || op == HIROpcode::ReturnVoid ||
           op == HIROpcode::Throw || op == HIROpcode::Unreachable;
}

std::shared_ptr<HIRValue> ASTToHIR::boxValueIfNeeded(std::shared_ptr<HIRValue> value) {
    // If value is already Any/ptr type, no boxing needed
    if (!value->type || value->type->kind == HIRTypeKind::Any ||
        value->type->kind == HIRTypeKind::Ptr) {
        return value;
    }

    // Box based on value type
    switch (value->type->kind) {
        case HIRTypeKind::Int64:
            return builder_.createBoxInt(value);
        case HIRTypeKind::Float64:
            return builder_.createBoxFloat(value);
        case HIRTypeKind::Bool:
            return builder_.createBoxBool(value);
        case HIRTypeKind::String:
            return builder_.createBoxString(value);
        case HIRTypeKind::Object:
        case HIRTypeKind::Array:
        case HIRTypeKind::Function:
        case HIRTypeKind::Class:
            return builder_.createBoxObject(value);
        default:
            // Already a ptr-like type, return as is
            return value;
    }
}

//==============================================================================
// Type Conversion
//==============================================================================

std::shared_ptr<HIRType> ASTToHIR::convertTypeFromString(const std::string& typeStr) {
    if (typeStr.empty()) {
        return HIRType::makeAny();
    }

    // Handle basic TypeScript type names
    if (typeStr == "number") {
        // In TypeScript, 'number' is always IEEE 754 double-precision float
        return HIRType::makeFloat64();
    } else if (typeStr == "string") {
        return HIRType::makeString();
    } else if (typeStr == "boolean") {
        return HIRType::makeBool();
    } else if (typeStr == "void") {
        return HIRType::makeVoid();
    } else if (typeStr == "null") {
        return HIRType::makePtr();
    } else if (typeStr == "undefined") {
        return HIRType::makePtr();
    } else if (typeStr == "any") {
        return HIRType::makeAny();
    } else if (typeStr == "unknown") {
        return HIRType::makeAny();
    } else if (typeStr == "object") {
        return HIRType::makeObject();
    } else if (typeStr == "never") {
        return HIRType::makeVoid();
    } else if (typeStr.find("[]") != std::string::npos) {
        // Array type like "number[]"
        std::string elemType = typeStr.substr(0, typeStr.length() - 2);
        return HIRType::makeArray(convertTypeFromString(elemType));
    } else if (typeStr.find("Array<") == 0) {
        // Array<T> syntax
        size_t start = 6;  // Length of "Array<"
        size_t end = typeStr.rfind('>');
        if (end != std::string::npos && end > start) {
            std::string elemType = typeStr.substr(start, end - start);
            return HIRType::makeArray(convertTypeFromString(elemType));
        }
        return HIRType::makeArray(HIRType::makeAny());
    } else if (typeStr.find("Promise<") == 0) {
        // Promise<T> - treat as ptr for now
        return HIRType::makePtr();
    } else if (typeStr.find("=>") != std::string::npos) {
        // Arrow function type syntax like "() => number" or "(x: number) => number"
        // These are function types, represented as pointers (closures)
        auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
        // Parse the return type after "=>"
        size_t arrowPos = typeStr.find("=>");
        if (arrowPos != std::string::npos) {
            std::string retTypeStr = typeStr.substr(arrowPos + 2);
            // Trim leading whitespace
            while (!retTypeStr.empty() && (retTypeStr[0] == ' ' || retTypeStr[0] == '\t')) {
                retTypeStr = retTypeStr.substr(1);
            }
            funcType->returnType = convertTypeFromString(retTypeStr);
        } else {
            funcType->returnType = HIRType::makeAny();
        }
        return funcType;
    }

    // Unknown type - could be a class name, treat as object
    return HIRType::makeObject();
}

std::shared_ptr<HIRType> ASTToHIR::convertType(const std::shared_ptr<ts::Type>& type) {
    if (!type) {
        return HIRType::makeAny();
    }

    switch (type->kind) {
        case ts::TypeKind::Void:
            return HIRType::makeVoid();
        case ts::TypeKind::Boolean:
            return HIRType::makeBool();
        case ts::TypeKind::Int:
            return HIRType::makeInt64();
        case ts::TypeKind::Double:
            return HIRType::makeFloat64();
        case ts::TypeKind::String:
            return HIRType::makeString();
        case ts::TypeKind::Any:
        case ts::TypeKind::Unknown:
            return HIRType::makeAny();
        case ts::TypeKind::Null:
        case ts::TypeKind::Undefined:
            return HIRType::makePtr();  // null/undefined are ptr type
        case ts::TypeKind::Array:
            if (auto arrType = std::dynamic_pointer_cast<ts::ArrayType>(type)) {
                return HIRType::makeArray(convertType(arrType->elementType));
            }
            return HIRType::makeArray(HIRType::makeAny());
        case ts::TypeKind::Object:
            return HIRType::makeObject();
        case ts::TypeKind::Class: {
            // Preserve class type information including the class name
            if (auto classType = std::dynamic_pointer_cast<ts::ClassType>(type)) {
                return HIRType::makeClass(classType->name, 0);
            }
            return HIRType::makeObject();  // Fallback to generic object
        }
        case ts::TypeKind::BigInt:
            return HIRType::makeObject();  // BigInt is a heap-allocated object
        case ts::TypeKind::Function: {
            // Preserve function type information for closures
            auto funcType = std::dynamic_pointer_cast<ts::FunctionType>(type);
            if (funcType) {
                auto hirFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
                for (const auto& paramType : funcType->paramTypes) {
                    hirFuncType->paramTypes.push_back(convertType(paramType));
                }
                if (funcType->returnType) {
                    hirFuncType->returnType = convertType(funcType->returnType);
                } else {
                    hirFuncType->returnType = HIRType::makeAny();
                }
                return hirFuncType;
            }
            return HIRType::makePtr();  // Fallback to generic pointer
        }
        default:
            return HIRType::makeAny();
    }
}

//==============================================================================
// Deferred Static Initialization
//==============================================================================

void ASTToHIR::emitDeferredStaticInits() {
    // Emit static property initializations
    for (auto& init : deferredStaticInits_) {
        auto initVal = lowerExpression(init.initExpr);
        builder_.createStore(initVal, init.globalPtr, init.propType);
    }
    deferredStaticInits_.clear();  // Only emit once

    // Emit static blocks
    for (auto* staticBlock : deferredStaticBlocks_) {
        for (auto& stmt : staticBlock->body) {
            lowerStatement(stmt.get());
        }
    }
    deferredStaticBlocks_.clear();  // Only emit once
}

//==============================================================================
// Statement Lowering
//==============================================================================

void ASTToHIR::lowerStatement(ast::Statement* stmt) {
    stmt->accept(this);
}

std::shared_ptr<HIRValue> ASTToHIR::lowerExpression(ast::Expression* expr) {
    lastValue_ = nullptr;
    expr->accept(this);
    return lastValue_;
}

void ASTToHIR::visitProgram(ast::Program* node) {
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }
}

void ASTToHIR::visitFunctionDeclaration(ast::FunctionDeclaration* node) {
    // Create HIR function - HIRFunction constructor requires a name
    auto func = std::make_unique<HIRFunction>(node->name);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;

    // Handle parameters
    for (size_t paramIdx = 0; paramIdx < node->parameters.size(); ++paramIdx) {
        auto& param = node->parameters[paramIdx];
        // Convert parameter type from string if available
        auto paramType = param->type.empty()
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        // Get parameter name from NodePtr (it's a unique_ptr<Node>)
        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }

        // Check if this is a rest parameter (...args)
        if (param->isRest) {
            func->hasRestParam = true;
            func->restParamIndex = paramIdx;
            // Rest parameter should be an array type
            if (paramType->kind != HIRTypeKind::Array) {
                // Wrap in array type if not already
                paramType = HIRType::makeArray(paramType, false);
            }
        }

        func->params.push_back({paramName, paramType});
    }

    // Use declared return type if available, otherwise default to Any
    func->returnType = node->returnType.empty()
        ? HIRType::makeAny()
        : convertTypeFromString(node->returnType);

    // Save current function and create entry block
    HIRFunction* savedFunc = currentFunction_;
    currentFunction_ = func.get();

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Update function's value counter to start after parameters BEFORE the loop
    // This ensures values created during parameter processing (allocas, etc.)
    // don't conflict with parameter IDs 0, 1, 2, ...
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Register parameters in the scope so they can be looked up
    // Parameter values have IDs 0, 1, 2, ... matching their index in HIRToLLVM
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        // Create a value representing this parameter with specific ID
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

        // Check if this parameter has a default value
        ast::Parameter* astParam = (i < node->parameters.size()) ? node->parameters[i].get() : nullptr;
        if (astParam && astParam->initializer) {
            // Parameter has a default value - need to check if undefined and use default
            // Create an alloca to store the potentially-defaulted value
            auto allocaVal = builder_.createAlloca(paramType);

            // Check if param is undefined using runtime function
            // We can't use pointer comparison because ts_value_make_undefined() creates
            // a new TsValue* each time, so pointers won't match. Instead use the
            // runtime's ts_value_is_undefined() which checks the type field.
            auto isUndefined = builder_.createCall("ts_value_is_undefined",
                {paramValue}, HIRType::makeBool());

            // Create basic blocks for the conditional
            auto defaultBB = func->createBlock("default_param");
            auto usedBB = func->createBlock("use_param");
            auto mergeBB = func->createBlock("param_merge");

            // Branch based on undefined check
            builder_.createCondBranch(isUndefined, defaultBB, usedBB);

            // Default block - evaluate default expression and store
            builder_.setInsertPoint(defaultBB);
            currentBlock_ = defaultBB;
            auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
            auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
            builder_.createStore(defaultVal, allocaVal);
            builder_.createBranch(mergeBB);

            // Use param block - store the passed parameter value
            builder_.setInsertPoint(usedBB);
            currentBlock_ = usedBB;
            builder_.createStore(paramValue, allocaVal);
            builder_.createBranch(mergeBB);

            // Merge block - continue execution
            builder_.setInsertPoint(mergeBB);
            currentBlock_ = mergeBB;

            // Register the alloca as the variable (loads will get the correct value)
            defineVariableAlloca(paramName, allocaVal, paramType);
        } else {
            // No default value - just register the parameter directly
            defineVariable(paramName, paramValue);
        }
    }

    // If this is user_main, emit deferred static property initializations
    if (node->name == "user_main") {
        emitDeferredStaticInits();
    }

    // Lower function body
    for (size_t i = 0; i < node->body.size(); ++i) {
        auto& stmt = node->body[i];
        lowerStatement(stmt.get());
        // Stop processing statements after a terminator (return, throw, etc.)
        // This prevents dead code from being emitted after control flow ends
        if (builder_.isBlockTerminated()) {
            break;
        }
    }

    // Add implicit return if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    popScope();

    // Restore saved function
    currentFunction_ = savedFunc;
    if (savedFunc) {
        auto* savedBlock = savedFunc->getEntryBlock();
        builder_.setInsertPoint(savedBlock);
        currentBlock_ = savedBlock;
    }

    // Add function to module
    module_->functions.push_back(std::move(func));
}

void ASTToHIR::visitVariableDeclaration(ast::VariableDeclaration* node) {
    // VariableDeclaration has name (NodePtr) and initializer (ExprPtr)
    // name can be Identifier, ObjectBindingPattern, or ArrayBindingPattern

    // Lower the initializer first (if any)
    std::shared_ptr<HIRValue> initValue;
    if (node->initializer) {
        initValue = lowerExpression(node->initializer.get());

        // Track class expression assignments: const MyClass = class { ... }
        if (dynamic_cast<ast::ClassExpression*>(node->initializer.get())) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
                // Map the variable name to the generated class name
                variableToClassName_[ident->name] = lastGeneratedClassName_;
            }
        }
    } else {
        initValue = builder_.createConstUndefined();
    }

    // Handle the binding pattern
    if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
        // Simple identifier - create variable directly
        std::shared_ptr<HIRType> varType = HIRType::makeAny();
        if (node->initializer && node->initializer->inferredType) {
            varType = convertType(node->initializer->inferredType);
        }
        if (!node->type.empty() && varType->kind == HIRTypeKind::Any) {
            varType = convertTypeFromString(node->type);
        }
        // Fallback: if type is still Any but initValue has a more specific type, use that
        // This handles cases like `const map = new Map()` where the lowered expression knows the type
        if (varType->kind == HIRTypeKind::Any && initValue && initValue->type && initValue->type->kind != HIRTypeKind::Any) {
            varType = initValue->type;
        }

        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(initValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);
    } else if (auto* objPattern = dynamic_cast<ast::ObjectBindingPattern*>(node->name.get())) {
        // Object destructuring: const { a, b } = obj
        lowerObjectBindingPattern(objPattern, initValue);
    } else if (auto* arrPattern = dynamic_cast<ast::ArrayBindingPattern*>(node->name.get())) {
        // Array destructuring: const [a, b] = arr
        lowerArrayBindingPattern(arrPattern, initValue);
    }
}

void ASTToHIR::lowerObjectBindingPattern(ast::ObjectBindingPattern* pattern,
                                          std::shared_ptr<HIRValue> sourceValue) {
    for (auto& elem : pattern->elements) {
        if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
            lowerBindingElement(binding, sourceValue, true /* isObjectPattern */);
        }
    }
}

void ASTToHIR::lowerArrayBindingPattern(ast::ArrayBindingPattern* pattern,
                                         std::shared_ptr<HIRValue> sourceValue) {
    int64_t index = 0;
    for (auto& elem : pattern->elements) {
        if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
            if (binding->isSpread) {
                // Rest element: ...rest - get remaining elements
                lowerRestElement(binding, sourceValue, index);
            } else {
                // Regular element - extract by index
                lowerBindingElementByIndex(binding, sourceValue, index);
            }
            index++;
        } else if (dynamic_cast<ast::OmittedExpression*>(elem.get())) {
            // Hole in array pattern: [a, , b] - skip this index
            index++;
        }
    }
}

void ASTToHIR::lowerBindingElement(ast::BindingElement* binding,
                                    std::shared_ptr<HIRValue> sourceValue,
                                    bool isObjectPattern) {
    // Determine the property name to extract
    std::string propName;
    if (!binding->propertyName.empty()) {
        // { propName: varName } - use explicit property name
        propName = binding->propertyName;
    } else if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        // { varName } - shorthand, property name is same as variable name
        propName = ident->name;
    }

    // Get the property value from source object
    auto propNameValue = builder_.createConstString(propName);
    auto extractedValue = builder_.createGetPropDynamic(sourceValue, propNameValue);

    // Handle default value if present
    if (binding->initializer) {
        // Check if extracted value is undefined using runtime function
        auto isUndefined = builder_.createIsUndefined(extractedValue);
        auto defaultValue = lowerExpression(binding->initializer.get());

        // Box the default value to match extractedValue type (Any/ptr)
        defaultValue = boxValueIfNeeded(defaultValue);

        // Select: isUndefined ? defaultValue : extractedValue
        extractedValue = builder_.createSelect(isUndefined, defaultValue, extractedValue);
    }

    // Bind to variable(s)
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        // Simple variable binding
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(extractedValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);
    } else if (auto* nestedObj = dynamic_cast<ast::ObjectBindingPattern*>(binding->name.get())) {
        // Nested object destructuring: { a: { b, c } }
        lowerObjectBindingPattern(nestedObj, extractedValue);
    } else if (auto* nestedArr = dynamic_cast<ast::ArrayBindingPattern*>(binding->name.get())) {
        // Nested array destructuring: { a: [b, c] }
        lowerArrayBindingPattern(nestedArr, extractedValue);
    }
}

void ASTToHIR::lowerBindingElementByIndex(ast::BindingElement* binding,
                                           std::shared_ptr<HIRValue> sourceValue,
                                           int64_t index) {
    // Get the element at index from source array
    // Force result type to Any so the value stays boxed (won't be unboxed in HIRToLLVM)
    auto indexValue = builder_.createConstInt(index);
    auto extractedValue = builder_.createGetElem(sourceValue, indexValue, HIRType::makeAny());

    // Handle default value if present
    if (binding->initializer) {
        // Check if extracted value is undefined using runtime function
        auto isUndefined = builder_.createIsUndefined(extractedValue);
        auto defaultValue = lowerExpression(binding->initializer.get());

        // Box the default value to match extractedValue (Any/ptr)
        defaultValue = boxValueIfNeeded(defaultValue);

        // Select: isUndefined ? defaultValue : extractedValue
        extractedValue = builder_.createSelect(isUndefined, defaultValue, extractedValue);
    }

    // Bind to variable(s)
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(extractedValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);
    } else if (auto* nestedObj = dynamic_cast<ast::ObjectBindingPattern*>(binding->name.get())) {
        lowerObjectBindingPattern(nestedObj, extractedValue);
    } else if (auto* nestedArr = dynamic_cast<ast::ArrayBindingPattern*>(binding->name.get())) {
        lowerArrayBindingPattern(nestedArr, extractedValue);
    }
}

void ASTToHIR::lowerRestElement(ast::BindingElement* binding,
                                 std::shared_ptr<HIRValue> sourceValue,
                                 int64_t startIndex) {
    // Create a new array with remaining elements using array.slice(startIndex)
    auto startIndexValue = builder_.createConstInt(startIndex);
    std::vector<std::shared_ptr<HIRValue>> sliceArgs = { startIndexValue };
    auto restValue = builder_.createCallMethod(sourceValue, "slice", sliceArgs, HIRType::makeAny());

    // Bind to variable
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(restValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);
    }
}

void ASTToHIR::visitExpressionStatement(ast::ExpressionStatement* node) {
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitBlockStatement(ast::BlockStatement* node) {
    pushScope();
    for (auto& stmt : node->statements) {
        lowerStatement(stmt.get());
        // Stop processing statements after a terminator (return, throw, etc.)
        // This prevents dead code from being emitted after control flow ends
        if (builder_.isBlockTerminated()) {
            break;
        }
    }
    popScope();
}

void ASTToHIR::visitReturnStatement(ast::ReturnStatement* node) {
    if (node->expression) {
        auto retVal = lowerExpression(node->expression.get());
        builder_.createReturn(retVal);
    } else {
        builder_.createReturnVoid();
    }
}

void ASTToHIR::visitIfStatement(ast::IfStatement* node) {
    auto cond = lowerExpression(node->condition.get());

    auto* thenBlock = createBlock("if.then");
    auto* elseBlock = createBlock("if.else");
    auto* mergeBlock = createBlock("if.end");

    builder_.createCondBranch(cond, thenBlock, elseBlock);

    // Then block
    builder_.setInsertPoint(thenBlock);
    currentBlock_ = thenBlock;
    lowerStatement(node->thenStatement.get());
    emitBranchIfNeeded(mergeBlock);

    // Else block
    builder_.setInsertPoint(elseBlock);
    currentBlock_ = elseBlock;
    if (node->elseStatement) {
        lowerStatement(node->elseStatement.get());
    }
    emitBranchIfNeeded(mergeBlock);

    // Continue in merge block
    builder_.setInsertPoint(mergeBlock);
    currentBlock_ = mergeBlock;
}

void ASTToHIR::visitWhileStatement(ast::WhileStatement* node) {
    auto* condBlock = createBlock("while.cond");
    auto* bodyBlock = createBlock("while.body");
    auto* endBlock = createBlock("while.end");

    // Push loop context for break/continue
    LoopContext ctx = {condBlock, endBlock};
    loopStack_.push(ctx);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    builder_.createBranch(condBlock);

    // Condition block
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    auto cond = lowerExpression(node->condition.get());
    builder_.createCondBranch(cond, bodyBlock, endBlock);

    // Body block
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;
    lowerStatement(node->body.get());
    emitBranchIfNeeded(condBlock);

    loopStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForStatement(ast::ForStatement* node) {
    auto* condBlock = createBlock("for.cond");
    auto* bodyBlock = createBlock("for.body");
    auto* updateBlock = createBlock("for.update");
    auto* endBlock = createBlock("for.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock};
    loopStack_.push(ctx);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    pushScope();

    // Initializer
    if (node->initializer) {
        lowerStatement(node->initializer.get());
    }

    builder_.createBranch(condBlock);

    // Condition block
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    if (node->condition) {
        auto cond = lowerExpression(node->condition.get());
        builder_.createCondBranch(cond, bodyBlock, endBlock);
    } else {
        // Infinite loop without condition
        builder_.createBranch(bodyBlock);
    }

    // Body block
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;
    lowerStatement(node->body.get());
    emitBranchIfNeeded(updateBlock);

    // Update block
    builder_.setInsertPoint(updateBlock);
    currentBlock_ = updateBlock;
    if (node->incrementor) {
        lowerExpression(node->incrementor.get());
    }
    builder_.createBranch(condBlock);

    loopStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForOfStatement(ast::ForOfStatement* node) {
    // For-of loop: iterate over array elements
    auto* condBlock = createBlock("forof.cond");
    auto* bodyBlock = createBlock("forof.body");
    auto* updateBlock = createBlock("forof.update");
    auto* endBlock = createBlock("forof.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock};
    loopStack_.push(ctx);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    pushScope();

    // Get the iterable
    auto* iterExpr = dynamic_cast<ast::Expression*>(node->expression.get());
    auto iterable = iterExpr ? lowerExpression(iterExpr) : createValue(HIRType::makeAny());

    // Get array length
    auto lenVal = builder_.createArrayLength(iterable);

    // Create index variable (alloca for SSA)
    auto indexAlloca = builder_.createAlloca(HIRType::makeInt64(), "forof.idx");
    auto zero = builder_.createConstInt(0);
    builder_.createStore(zero, indexAlloca);

    builder_.createBranch(condBlock);

    // Condition: index < length
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    auto indexVal = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto cond = builder_.createCmpLtI64(indexVal, lenVal);
    builder_.createCondBranch(cond, bodyBlock, endBlock);

    // Body: get element and execute body
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;

    // Get current element
    auto currentIndex = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto elemVal = builder_.createGetElem(iterable, currentIndex);

    // Bind to loop variable
    if (node->initializer) {
        auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
        if (varDecl) {
            // VariableDeclaration has name directly (not declarations array)
            auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get());
            if (ident) {
                defineVariable(ident->name, elemVal);
            }
        }
    }

    lowerStatement(node->body.get());

    // Branch to update (if not already terminated)
    emitBranchIfNeeded(updateBlock);

    // Update block: increment index
    builder_.setInsertPoint(updateBlock);
    currentBlock_ = updateBlock;
    auto idxForInc = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto one = builder_.createConstInt(1);
    auto newIndex = builder_.createAddI64(idxForInc, one);
    builder_.createStore(newIndex, indexAlloca);
    builder_.createBranch(condBlock);

    loopStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForInStatement(ast::ForInStatement* node) {
    // For-in loop: iterate over object keys
    // Implementation: Get Object.keys(obj), then iterate over the array
    auto* condBlock = createBlock("forin.cond");
    auto* bodyBlock = createBlock("forin.body");
    auto* updateBlock = createBlock("forin.update");
    auto* endBlock = createBlock("forin.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock};
    loopStack_.push(ctx);

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    pushScope();

    // Get the object to iterate
    auto* objExpr = dynamic_cast<ast::Expression*>(node->expression.get());
    auto obj = objExpr ? lowerExpression(objExpr) : createValue(HIRType::makeObject());

    // Get keys array: Object.keys(obj)
    auto keys = builder_.createCall("ts_object_keys", {obj}, HIRType::makeArray(HIRType::makeString()));

    // Get array length
    auto length = builder_.createArrayLength(keys);

    // Create index variable (alloca for SSA)
    auto indexAlloca = builder_.createAlloca(HIRType::makeInt64(), "forin.idx");
    auto zero = builder_.createConstInt(0);
    builder_.createStore(zero, indexAlloca);

    // Branch to condition
    builder_.createBranch(condBlock);

    // Condition block: check index < length
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    auto indexVal = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto cond = builder_.createCmpLtI64(indexVal, length);
    builder_.createCondBranch(cond, bodyBlock, endBlock);

    // Body block
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;

    // Get current key: keys[index]
    auto currentIndex = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto key = builder_.createGetElem(keys, currentIndex);

    // Bind to loop variable
    if (node->initializer) {
        auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
        if (varDecl) {
            auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get());
            if (ident) {
                defineVariable(ident->name, key);
            }
        }
    }

    // Lower body
    lowerStatement(node->body.get());

    // Branch to update (if not already terminated)
    emitBranchIfNeeded(updateBlock);

    // Update block: increment index
    builder_.setInsertPoint(updateBlock);
    currentBlock_ = updateBlock;
    auto idxForInc = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
    auto one = builder_.createConstInt(1);
    auto newIndex = builder_.createAddI64(idxForInc, one);
    builder_.createStore(newIndex, indexAlloca);
    builder_.createBranch(condBlock);

    loopStack_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitBreakStatement(ast::BreakStatement* node) {
    if (!node->label.empty()) {
        // Labeled break - find the labeled loop
        auto it = labeledLoops_.find(node->label);
        if (it != labeledLoops_.end()) {
            builder_.createBranch(it->second.breakTarget);
        }
    } else if (!loopStack_.empty()) {
        // Unlabeled break - use innermost loop
        builder_.createBranch(loopStack_.top().breakTarget);
    } else if (!switchStack_.empty()) {
        // Unlabeled break inside switch (but not inside a loop within the switch)
        builder_.createBranch(switchStack_.top().breakTarget);
    }
}

void ASTToHIR::visitContinueStatement(ast::ContinueStatement* node) {
    if (!node->label.empty()) {
        // Labeled continue - find the labeled loop
        auto it = labeledLoops_.find(node->label);
        if (it != labeledLoops_.end()) {
            builder_.createBranch(it->second.continueTarget);
        }
    } else if (!loopStack_.empty()) {
        // Unlabeled continue - use innermost loop
        builder_.createBranch(loopStack_.top().continueTarget);
    }
}

void ASTToHIR::visitLabeledStatement(ast::LabeledStatement* node) {
    // Set the pending label - the next loop will register itself with this label
    std::string savedLabel = pendingLabel_;
    pendingLabel_ = node->label;

    // Lower the statement (the loop will pick up pendingLabel_)
    lowerStatement(node->statement.get());

    // Clean up the label registration (in case the loop registered it)
    labeledLoops_.erase(node->label);

    // Restore any outer pending label
    pendingLabel_ = savedLabel;
}

void ASTToHIR::visitSwitchStatement(ast::SwitchStatement* node) {
    auto switchVal = lowerExpression(node->expression.get());

    auto* endBlock = createBlock("switch.end");
    switchStack_.push({endBlock, {}, nullptr});

    std::vector<HIRBlock*> caseBlocks;
    HIRBlock* defaultBlock = endBlock;

    // Create blocks for each case
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        auto* defaultClause = dynamic_cast<ast::DefaultClause*>(clause.get());

        if (caseClause) {
            auto* caseBlock = createBlock("switch.case");
            caseBlocks.push_back(caseBlock);
        } else if (defaultClause) {
            defaultBlock = createBlock("switch.default");
            caseBlocks.push_back(defaultBlock);
        }
    }

    // Build switch cases
    std::vector<std::pair<int64_t, HIRBlock*>> cases;
    size_t blockIdx = 0;
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        if (caseClause && caseClause->expression) {
            // Try to get constant value
            auto* numLit = dynamic_cast<ast::NumericLiteral*>(caseClause->expression.get());
            if (numLit && blockIdx < caseBlocks.size()) {
                cases.push_back({static_cast<int64_t>(numLit->value), caseBlocks[blockIdx]});
            }
        }
        blockIdx++;
    }

    builder_.createSwitch(switchVal, defaultBlock, cases);

    // Generate code for each case
    blockIdx = 0;
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        auto* defaultClause = dynamic_cast<ast::DefaultClause*>(clause.get());

        HIRBlock* block = (blockIdx < caseBlocks.size()) ? caseBlocks[blockIdx] : nullptr;
        if (!block) continue;

        builder_.setInsertPoint(block);
        currentBlock_ = block;

        if (caseClause) {
            for (auto& stmt : caseClause->statements) {
                lowerStatement(stmt.get());
            }
        } else if (defaultClause) {
            for (auto& stmt : defaultClause->statements) {
                lowerStatement(stmt.get());
            }
        }

        // Fall through to next case or end
        if (!hasTerminator()) {
            if (blockIdx + 1 < caseBlocks.size()) {
                builder_.createBranch(caseBlocks[blockIdx + 1]);
            } else {
                builder_.createBranch(endBlock);
            }
        }

        blockIdx++;
    }

    switchStack_.pop();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitTryStatement(ast::TryStatement* node) {
    // Create basic blocks for exception handling control flow
    // Use createBlock (with unique numbering) to handle nested try statements
    auto tryBB = createBlock("try");
    auto catchBB = node->catchClause ? createBlock("catch") : nullptr;
    auto finallyBB = !node->finallyBlock.empty() ? createBlock("finally") : nullptr;
    auto mergeBB = createBlock("try.merge");

    // When there's finally but no catch, we need an intermediate block to store the exception
    HIRBlock* exceptionStoreBB = nullptr;
    if (finallyBB && !catchBB) {
        exceptionStoreBB = createBlock("try.store_exception");
    }

    // Determine where to go after try/catch
    HIRBlock* afterTryDest = finallyBB ? finallyBB : mergeBB;
    HIRBlock* afterCatchDest = finallyBB ? finallyBB : mergeBB;

    // Determine where to go on exception
    HIRBlock* exceptionDest = catchBB ? catchBB : (exceptionStoreBB ? exceptionStoreBB : afterTryDest);

    // Create alloca for pending exception (for finally rethrow)
    std::shared_ptr<HIRValue> pendingExc = nullptr;
    if (finallyBB) {
        pendingExc = builder_.createAlloca(HIRType::makeAny());
        builder_.createStore(builder_.createConstNull(), pendingExc);
    }

    // Setup try: push handler and call setjmp
    // Returns true if we're coming from an exception, false on normal entry
    auto isException = builder_.createSetupTry(exceptionDest);
    builder_.createCondBranch(isException, exceptionDest, tryBB);

    // --- Try Block ---
    builder_.setInsertPoint(tryBB);
    currentBlock_ = tryBB;

    for (auto& stmt : node->tryBlock) {
        if (hasTerminator()) break;  // Stop if block already terminated (e.g., by throw)
        lowerStatement(stmt.get());
    }

    // Pop exception handler and branch to finally/merge
    if (currentBlock_->getTerminator() == nullptr) {
        builder_.createPopHandler();
        builder_.createBranch(afterTryDest);
    }

    // --- Catch Block ---
    if (catchBB && node->catchClause) {
        builder_.setInsertPoint(catchBB);
        currentBlock_ = catchBB;

        // Get and clear the exception
        auto exception = builder_.createGetException();
        builder_.createClearException();

        // Bind exception to catch variable if present
        if (node->catchClause->variable) {
            // The variable could be an Identifier or a binding pattern
            if (auto* id = dynamic_cast<ast::Identifier*>(node->catchClause->variable.get())) {
                defineVariable(id->name, exception);
            }
            // TODO: Handle destructuring in catch clause
        }

        // Execute catch block statements
        for (auto& stmt : node->catchClause->block) {
            if (hasTerminator()) break;  // Stop if block already terminated
            lowerStatement(stmt.get());
        }

        // Branch to finally/merge
        if (currentBlock_->getTerminator() == nullptr) {
            builder_.createBranch(afterCatchDest);
        }
    }

    // --- Exception Store Block (for try-finally without catch) ---
    if (exceptionStoreBB) {
        builder_.setInsertPoint(exceptionStoreBB);
        currentBlock_ = exceptionStoreBB;

        // Get the exception and store it for later rethrow
        auto exception = builder_.createGetException();
        builder_.createStore(exception, pendingExc);
        builder_.createBranch(finallyBB);
    }

    // --- Finally Block ---
    if (finallyBB) {
        builder_.setInsertPoint(finallyBB);
        currentBlock_ = finallyBB;

        // Execute finally block statements
        for (auto& stmt : node->finallyBlock) {
            if (hasTerminator()) break;  // Stop if block already terminated
            lowerStatement(stmt.get());
        }

        // Check for pending exception to rethrow
        if (currentBlock_->getTerminator() == nullptr) {
            if (pendingExc) {
                auto exc = builder_.createLoad(HIRType::makeAny(), pendingExc);
                auto isNull = builder_.createCmpEqPtr(exc, builder_.createConstNull());

                auto rethrowBB = createBlock("try.rethrow");
                builder_.createCondBranch(isNull, mergeBB, rethrowBB);

                builder_.setInsertPoint(rethrowBB);
                currentBlock_ = rethrowBB;
                builder_.createThrow(exc);
            } else {
                builder_.createBranch(mergeBB);
            }
        }
    }

    // --- Merge Block ---
    builder_.setInsertPoint(mergeBB);
    currentBlock_ = mergeBB;
}

void ASTToHIR::visitThrowStatement(ast::ThrowStatement* node) {
    // Lower the throw expression
    std::shared_ptr<HIRValue> exception;
    if (node->expression) {
        exception = lowerExpression(node->expression.get());
        // Box the value if needed (throw can accept any value)
        if (exception->type && exception->type->kind != HIRTypeKind::Any) {
            exception = builder_.createBoxObject(exception);
        }
    } else {
        // throw; without expression - rethrow current exception
        exception = builder_.createGetException();
    }

    builder_.createThrow(exception);
}

void ASTToHIR::visitImportDeclaration(ast::ImportDeclaration* node) {
    // Imports are handled at module resolution time
    // Nothing to do at HIR level
}

void ASTToHIR::visitExportDeclaration(ast::ExportDeclaration* node) {
    // Exports are handled at module resolution time
    // ExportDeclaration has moduleSpecifier, namedExports, isStarExport, namespaceExport
    // but no direct declaration - nothing to lower at HIR level
}

void ASTToHIR::visitExportAssignment(ast::ExportAssignment* node) {
    // Lower the expression
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

//==============================================================================
// Expression Lowering
//==============================================================================

void ASTToHIR::visitBinaryExpression(ast::BinaryExpression* node) {
    const std::string& op = node->op;

    // Handle nullish coalescing with short-circuit semantics
    if (op == "??") {
        // Lower left side first
        auto lhs = lowerExpression(node->left.get());

        // Box lhs to Any if needed (for consistent phi node type)
        auto boxedLhs = boxValueIfNeeded(lhs);

        // Check if lhs is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {boxedLhs}, HIRType::makeBool());

        // Create unique block names
        int blockId = blockCounter_++;
        auto* rhsBlock = builder_.createBlock("nullish_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("nullish_merge_" + std::to_string(blockId));

        auto* lhsBlock = builder_.getInsertBlock();

        // If nullish, evaluate rhs; otherwise use lhs
        builder_.createCondBranch(isNullish, rhsBlock, mergeBlock);

        // Evaluate rhs
        builder_.setInsertPoint(rhsBlock);
        auto rhs = lowerExpression(node->right.get());
        // Box rhs to Any if needed (for consistent phi node type)
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge with phi node - both values should now be Any/ptr type
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    // Handle instanceof operator - need to handle rhs specially as class reference
    if (op == "instanceof") {
        auto lhs = lowerExpression(node->left.get());

        // rhs should be a class identifier - don't evaluate it as an expression
        auto* ident = dynamic_cast<ast::Identifier*>(node->right.get());
        if (ident) {
            // Check for built-in types like Array
            if (ident->name == "Array") {
                // Arrays have a specific check using ts_array_is_array
                lastValue_ = builder_.createCall("ts_array_is_array", {lhs}, HIRType::makeBool());
                return;
            }

            // Look for the VTable global for this class
            std::string vtableGlobalName = ident->name + "_VTable_Global";
            auto vtablePtr = builder_.createLoadGlobal(vtableGlobalName);
            lastValue_ = builder_.createInstanceOf(lhs, vtablePtr);
        } else {
            // Can't resolve class, return false
            lastValue_ = builder_.createConstBool(false);
        }
        return;
    }

    // Handle 'in' operator - check if property exists in object
    if (op == "in") {
        auto lhs = lowerExpression(node->left.get());  // property key
        auto rhs = lowerExpression(node->right.get()); // object
        lastValue_ = builder_.createCall("ts_object_has_property", {rhs, lhs}, HIRType::makeBool());
        return;
    }

    auto lhs = lowerExpression(node->left.get());
    auto rhs = lowerExpression(node->right.get());

    // Helper to check if an operand is a string type
    auto isString = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        if (val && val->type && val->type->kind == HIRTypeKind::String) return true;
        if (astNode && astNode->inferredType && astNode->inferredType->kind == ts::TypeKind::String) return true;
        return false;
    };

    // Helper to check if an operand is Float64 type
    auto isFloat64 = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        if (val && val->type && val->type->kind == HIRTypeKind::Float64) return true;
        if (astNode && astNode->inferredType && astNode->inferredType->kind == ts::TypeKind::Double) return true;
        return false;
    };

    // Helper to check if an operand is BigInt type
    auto isBigInt = [](ast::Expression* astNode) {
        return astNode && astNode->inferredType &&
               astNode->inferredType->kind == ts::TypeKind::BigInt;
    };

    // Helper to check if an operand is a number type (Int or Double)
    auto isNumber = [&isFloat64](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        // Check HIR type
        if (val && val->type) {
            if (val->type->kind == HIRTypeKind::Int64 ||
                val->type->kind == HIRTypeKind::Float64) return true;
        }
        // Check AST inferred type
        if (astNode && astNode->inferredType) {
            if (astNode->inferredType->kind == ts::TypeKind::Int ||
                astNode->inferredType->kind == ts::TypeKind::Double) return true;
        }
        return false;
    };

    // Helper to check if an operand is a boolean type
    auto isBoolean = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        if (val && val->type && val->type->kind == HIRTypeKind::Bool) return true;
        if (astNode && astNode->inferredType && astNode->inferredType->kind == ts::TypeKind::Boolean) return true;
        return false;
    };

    // Helper to check if an operand could be Any, Null, or Undefined type
    // (requires runtime type checking for strict equality)
    auto isAnyOrNullish = [](const std::shared_ptr<HIRValue>& val, ast::Expression* astNode) {
        // Check HIR type
        if (val && val->type && val->type->kind == HIRTypeKind::Any) return true;
        // Check AST inferred type
        if (astNode && astNode->inferredType) {
            auto kind = astNode->inferredType->kind;
            if (kind == ts::TypeKind::Any || kind == ts::TypeKind::Undefined ||
                kind == ts::TypeKind::Null || kind == ts::TypeKind::Unknown) return true;
        }
        return false;
    };

    // Helper to check if an expression is the literal `undefined` keyword
    auto isUndefinedLiteral = [](ast::Expression* astNode) {
        if (auto* id = dynamic_cast<ast::Identifier*>(astNode)) {
            return id->name == "undefined";
        }
        return false;
    };

    // Helper to check if an expression is the literal `null` keyword
    auto isNullLiteral = [](ast::Expression* astNode) {
        if (auto* nullLit = dynamic_cast<ast::NullLiteral*>(astNode)) {
            return true;
        }
        if (auto* id = dynamic_cast<ast::Identifier*>(astNode)) {
            return id->name == "null";
        }
        return false;
    };

    // For strict equality (===), check if types are incompatible
    // Returns true if types are definitely different and === should return false
    auto typesIncompatibleForStrictEqual = [&isString, &isNumber, &isBoolean, &isBigInt](
            const std::shared_ptr<HIRValue>& lhsVal, ast::Expression* lhsAst,
            const std::shared_ptr<HIRValue>& rhsVal, ast::Expression* rhsAst) {
        bool lhsIsString = isString(lhsVal, lhsAst);
        bool rhsIsString = isString(rhsVal, rhsAst);
        bool lhsIsNumber = !lhsIsString && ((lhsVal && lhsVal->type &&
            (lhsVal->type->kind == HIRTypeKind::Int64 || lhsVal->type->kind == HIRTypeKind::Float64))
            || (lhsAst && lhsAst->inferredType &&
            (lhsAst->inferredType->kind == ts::TypeKind::Int ||
             lhsAst->inferredType->kind == ts::TypeKind::Double)));
        bool rhsIsNumber = !rhsIsString && ((rhsVal && rhsVal->type &&
            (rhsVal->type->kind == HIRTypeKind::Int64 || rhsVal->type->kind == HIRTypeKind::Float64))
            || (rhsAst && rhsAst->inferredType &&
            (rhsAst->inferredType->kind == ts::TypeKind::Int ||
             rhsAst->inferredType->kind == ts::TypeKind::Double)));
        bool lhsIsBoolean = isBoolean(lhsVal, lhsAst);
        bool rhsIsBoolean = isBoolean(rhsVal, rhsAst);
        bool lhsIsBigInt = isBigInt(lhsAst);
        bool rhsIsBigInt = isBigInt(rhsAst);

        // If both are the same type category, compatible
        if (lhsIsString && rhsIsString) return false;
        if (lhsIsNumber && rhsIsNumber) return false;
        if (lhsIsBoolean && rhsIsBoolean) return false;
        if (lhsIsBigInt && rhsIsBigInt) return false;

        // If one has a known type and the other has a different known type, incompatible
        if (lhsIsString && (rhsIsNumber || rhsIsBoolean || rhsIsBigInt)) return true;
        if (lhsIsNumber && (rhsIsString || rhsIsBoolean || rhsIsBigInt)) return true;
        if (lhsIsBoolean && (rhsIsString || rhsIsNumber || rhsIsBigInt)) return true;
        if (lhsIsBigInt && (rhsIsString || rhsIsNumber || rhsIsBoolean)) return true;

        // If types are unknown (Any), can't determine incompatibility at compile time
        return false;
    };

    // Determine if we should use Float64 operations (if either operand is Float64)
    bool useFloat = isFloat64(lhs, node->left.get()) || isFloat64(rhs, node->right.get());
    bool useBigInt = isBigInt(node->left.get()) || isBigInt(node->right.get());

    if (op == "+") {
        // Check if either operand is a string - if so, use string concatenation
        if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
            lastValue_ = builder_.createStringConcat(lhs, rhs);
        } else if (useBigInt) {
            // BigInt addition via runtime call
            lastValue_ = builder_.createCall("ts_bigint_add", {lhs, rhs}, HIRType::makeObject());
        } else if (useFloat) {
            lastValue_ = builder_.createAddF64(lhs, rhs);
        } else {
            lastValue_ = builder_.createAddI64(lhs, rhs);
        }
    } else if (op == "-") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_sub", {lhs, rhs}, HIRType::makeObject());
        } else {
            lastValue_ = useFloat ? builder_.createSubF64(lhs, rhs) : builder_.createSubI64(lhs, rhs);
        }
    } else if (op == "*") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_mul", {lhs, rhs}, HIRType::makeObject());
        } else {
            lastValue_ = useFloat ? builder_.createMulF64(lhs, rhs) : builder_.createMulI64(lhs, rhs);
        }
    } else if (op == "/") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_div", {lhs, rhs}, HIRType::makeObject());
        } else {
            lastValue_ = useFloat ? builder_.createDivF64(lhs, rhs) : builder_.createDivI64(lhs, rhs);
        }
    } else if (op == "%") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_mod", {lhs, rhs}, HIRType::makeObject());
        } else {
            lastValue_ = useFloat ? builder_.createModF64(lhs, rhs) : builder_.createModI64(lhs, rhs);
        }
    } else if (op == "<") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_lt", {lhs, rhs}, HIRType::makeBool());
        } else {
            lastValue_ = useFloat ? builder_.createCmpLtF64(lhs, rhs) : builder_.createCmpLtI64(lhs, rhs);
        }
    } else if (op == "<=") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_le", {lhs, rhs}, HIRType::makeBool());
        } else {
            lastValue_ = useFloat ? builder_.createCmpLeF64(lhs, rhs) : builder_.createCmpLeI64(lhs, rhs);
        }
    } else if (op == ">") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_gt", {lhs, rhs}, HIRType::makeBool());
        } else {
            lastValue_ = useFloat ? builder_.createCmpGtF64(lhs, rhs) : builder_.createCmpGtI64(lhs, rhs);
        }
    } else if (op == ">=") {
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ge", {lhs, rhs}, HIRType::makeBool());
        } else {
            lastValue_ = useFloat ? builder_.createCmpGeF64(lhs, rhs) : builder_.createCmpGeI64(lhs, rhs);
        }
    } else if (op == "==") {
        // Loose equality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else {
            lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
        }
    } else if (op == "===") {
        // Strict equality - if types are incompatible, return false directly
        if (typesIncompatibleForStrictEqual(lhs, node->left.get(), rhs, node->right.get())) {
            lastValue_ = builder_.createConstBool(false);
        } else if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isString(lhs, node->left.get()) && isString(rhs, node->right.get())) {
            // String comparison using ts_string_eq
            lastValue_ = builder_.createCall("ts_string_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x === undefined where x is Any type: use ts_value_is_undefined(x)
            // This correctly checks if a TsValue* has type == UNDEFINED
            lastValue_ = builder_.createCall("ts_value_is_undefined", {lhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // undefined === x where x is Any type: use ts_value_is_undefined(x)
            lastValue_ = builder_.createCall("ts_value_is_undefined", {rhs}, HIRType::makeBool());
        } else if (isNullLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x === null where x is Any type: use ts_value_is_null(x)
            lastValue_ = builder_.createCall("ts_value_is_null", {lhs}, HIRType::makeBool());
        } else if (isNullLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // null === x where x is Any type: use ts_value_is_null(x)
            lastValue_ = builder_.createCall("ts_value_is_null", {rhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->left.get()) && isUndefinedLiteral(node->right.get())) {
            // undefined === undefined is always true
            lastValue_ = builder_.createConstBool(true);
        } else if (isNullLiteral(node->left.get()) && isNullLiteral(node->right.get())) {
            // null === null is always true
            lastValue_ = builder_.createConstBool(true);
        } else {
            lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
        }
    } else if (op == "!=") {
        // Loose inequality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ne", {lhs, rhs}, HIRType::makeBool());
        } else {
            lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
        }
    } else if (op == "!==") {
        // Strict inequality - if types are incompatible, return true directly
        if (typesIncompatibleForStrictEqual(lhs, node->left.get(), rhs, node->right.get())) {
            lastValue_ = builder_.createConstBool(true);
        } else if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ne", {lhs, rhs}, HIRType::makeBool());
        } else if (isString(lhs, node->left.get()) && isString(rhs, node->right.get())) {
            // String comparison using ts_string_eq, then negate
            auto eq = builder_.createCall("ts_string_eq", {lhs, rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x !== undefined where x is Any type: negate ts_value_is_undefined(x)
            auto eq = builder_.createCall("ts_value_is_undefined", {lhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // undefined !== x where x is Any type: negate ts_value_is_undefined(x)
            auto eq = builder_.createCall("ts_value_is_undefined", {rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isNullLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x !== null where x is Any type: negate ts_value_is_null(x)
            auto eq = builder_.createCall("ts_value_is_null", {lhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isNullLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // null !== x where x is Any type: negate ts_value_is_null(x)
            auto eq = builder_.createCall("ts_value_is_null", {rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->left.get()) && isUndefinedLiteral(node->right.get())) {
            // undefined !== undefined is always false
            lastValue_ = builder_.createConstBool(false);
        } else if (isNullLiteral(node->left.get()) && isNullLiteral(node->right.get())) {
            // null !== null is always false
            lastValue_ = builder_.createConstBool(false);
        } else {
            lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
        }
    } else if (op == "&&") {
        lastValue_ = builder_.createLogicalAnd(lhs, rhs);
    } else if (op == "||") {
        lastValue_ = builder_.createLogicalOr(lhs, rhs);
    } else if (op == "&") {
        lastValue_ = builder_.createAndI64(lhs, rhs);
    } else if (op == "|") {
        lastValue_ = builder_.createOrI64(lhs, rhs);
    } else if (op == "^") {
        lastValue_ = builder_.createXorI64(lhs, rhs);
    } else if (op == "<<") {
        lastValue_ = builder_.createShlI64(lhs, rhs);
    } else if (op == ">>") {
        lastValue_ = builder_.createShrI64(lhs, rhs);
    } else if (op == ">>>") {
        lastValue_ = builder_.createUShrI64(lhs, rhs);
    } else if (op == ",") {
        // Comma operator: evaluate both sides for side effects, return right
        // lhs is already evaluated above, rhs is already evaluated above
        lastValue_ = rhs;
    } else if (op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
               op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" || op == ">>>=") {
        // Compound assignment operators
        // lhs already contains the loaded current value
        // rhs contains the value to add/subtract/etc.
        // We need to:
        // 1. Compute the new value
        // 2. Store it back to the LHS location
        // 3. Return the new value

        std::shared_ptr<HIRValue> result;

        // Compute the operation
        if (op == "+=") {
            if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
                result = builder_.createStringConcat(lhs, rhs);
            } else if (useBigInt) {
                result = builder_.createCall("ts_bigint_add", {lhs, rhs}, HIRType::makeObject());
            } else if (useFloat) {
                result = builder_.createAddF64(lhs, rhs);
            } else {
                result = builder_.createAddI64(lhs, rhs);
            }
        } else if (op == "-=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_sub", {lhs, rhs}, HIRType::makeObject());
            } else if (useFloat) {
                result = builder_.createSubF64(lhs, rhs);
            } else {
                result = builder_.createSubI64(lhs, rhs);
            }
        } else if (op == "*=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_mul", {lhs, rhs}, HIRType::makeObject());
            } else if (useFloat) {
                result = builder_.createMulF64(lhs, rhs);
            } else {
                result = builder_.createMulI64(lhs, rhs);
            }
        } else if (op == "/=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_div", {lhs, rhs}, HIRType::makeObject());
            } else if (useFloat) {
                result = builder_.createDivF64(lhs, rhs);
            } else {
                result = builder_.createDivI64(lhs, rhs);
            }
        } else if (op == "%=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_mod", {lhs, rhs}, HIRType::makeObject());
            } else if (useFloat) {
                result = builder_.createModF64(lhs, rhs);
            } else {
                result = builder_.createModI64(lhs, rhs);
            }
        } else if (op == "&=") {
            result = builder_.createAndI64(lhs, rhs);
        } else if (op == "|=") {
            result = builder_.createOrI64(lhs, rhs);
        } else if (op == "^=") {
            result = builder_.createXorI64(lhs, rhs);
        } else if (op == "<<=") {
            result = builder_.createShlI64(lhs, rhs);
        } else if (op == ">>=") {
            result = builder_.createShrI64(lhs, rhs);
        } else if (op == ">>>=") {
            result = builder_.createUShrI64(lhs, rhs);
        }

        // Now store the result back to the LHS
        // Handle identifier LHS
        auto* ident = dynamic_cast<ast::Identifier*>(node->left.get());
        if (ident) {
            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                builder_.createStore(result, info->value, info->elemType);
            } else if (info) {
                // Direct value - promote to alloca for mutability
                auto allocaPtr = builder_.createAlloca(result->type, ident->name);
                builder_.createStore(result, allocaPtr, result->type);
                info->value = allocaPtr;
                info->elemType = result->type;
                info->isAlloca = true;
            }
            lastValue_ = result;
            return;
        }

        // Handle property access LHS (e.g., obj.prop += val)
        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get());
        if (propAccess) {
            auto obj = lowerExpression(propAccess->expression.get());
            auto propName = builder_.createConstString(propAccess->name);
            std::vector<std::shared_ptr<HIRValue>> args = {obj, propName, boxValueIfNeeded(result)};
            builder_.createCall("ts_object_set_property", args, HIRType::makeVoid());
            lastValue_ = result;
            return;
        }

        // Handle element access LHS (e.g., arr[i] += val)
        auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get());
        if (elemAccess) {
            auto arr = lowerExpression(elemAccess->expression.get());
            auto idx = lowerExpression(elemAccess->argumentExpression.get());
            std::vector<std::shared_ptr<HIRValue>> args = {arr, idx, boxValueIfNeeded(result)};
            builder_.createCall("ts_array_set", args, HIRType::makeVoid());
            lastValue_ = result;
            return;
        }

        // Fallback - just return the computed value
        lastValue_ = result;
    } else {
        // Unknown operator - return lhs
        lastValue_ = lhs;
    }
}

void ASTToHIR::visitConditionalExpression(ast::ConditionalExpression* node) {
    auto cond = lowerExpression(node->condition.get());
    auto trueVal = lowerExpression(node->whenTrue.get());
    auto falseVal = lowerExpression(node->whenFalse.get());
    lastValue_ = builder_.createSelect(cond, trueVal, falseVal);
}

void ASTToHIR::visitAssignmentExpression(ast::AssignmentExpression* node) {
    auto rhs = lowerExpression(node->right.get());

    // Handle simple identifier assignment
    auto* ident = dynamic_cast<ast::Identifier*>(node->left.get());
    if (ident) {
        // Check if this is a captured variable from an outer function
        size_t scopeIndex = 0;
        if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
            // Store to captured variable
            auto* info = lookupVariableInfo(ident->name);
            auto type = info && info->elemType ? info->elemType : rhs->type;
            registerCapture(ident->name, type, scopeIndex);
            currentFunction_->hasClosure = true;
            builder_.createStoreCapture(ident->name, rhs);
            lastValue_ = rhs;
            return;
        }

        // Look up variable info to see if it's an alloca
        auto* info = lookupVariableInfo(ident->name);
        if (info && info->isAlloca) {
            // Emit store to the alloca, with type info for coercion
            builder_.createStore(rhs, info->value, info->elemType);
        } else if (info) {
            // Direct value - promote to alloca for mutability
            // Create new alloca and store
            auto allocaPtr = builder_.createAlloca(rhs->type, ident->name);
            builder_.createStore(rhs, allocaPtr, rhs->type);
            // Update variable info to be alloca-based
            info->value = allocaPtr;
            info->elemType = rhs->type;
            info->isAlloca = true;
        } else {
            // New variable - should not happen in assignment, but handle gracefully
            defineVariable(ident->name, rhs);
        }
        lastValue_ = rhs;
        return;
    }

    // Handle property access assignment
    auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get());
    if (propAccess) {
        // Check for static property assignment: ClassName.propertyName = value
        auto* classNameIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (classNameIdent) {
            for (auto& cls : module_->classes) {
                if (cls->name == classNameIdent->name) {
                    // Check if this is a static property
                    std::string globalName = cls->name + "_static_" + propAccess->name;
                    auto it = staticPropertyGlobals_.find(globalName);
                    if (it != staticPropertyGlobals_.end()) {
                        // Store to the static property global
                        auto globalPtr = it->second.first;
                        auto propType = it->second.second;
                        builder_.createStore(rhs, globalPtr, propType);
                        lastValue_ = rhs;
                        return;
                    }
                    break;
                }
            }
        }

        auto obj = lowerExpression(propAccess->expression.get());

        // Check for setter: look up the class type and see if it has __setter_<propName>
        HIRClass* targetClass = nullptr;

        // Check if expression has an inferred class type
        if (propAccess->expression && propAccess->expression->inferredType) {
            auto exprType = propAccess->expression->inferredType;
            if (exprType->kind == ts::TypeKind::Class) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
                if (classType) {
                    for (auto& cls : module_->classes) {
                        if (cls->name == classType->name) {
                            targetClass = cls.get();
                            break;
                        }
                    }
                }
            }
        }

        // If accessing 'this', use currentClass_
        if (!targetClass) {
            auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
            if (thisIdent && thisIdent->name == "this" && currentClass_) {
                targetClass = currentClass_;
            }
        }

        // Check if the target class has a setter for this property
        if (targetClass) {
            std::string setterKey = "__setter_" + propAccess->name;
            auto setterIt = targetClass->methods.find(setterKey);
            if (setterIt != targetClass->methods.end()) {
                // Found a setter - call it instead of direct property assignment
                HIRFunction* setterFunc = setterIt->second;
                builder_.createCall(setterFunc->name, {obj, rhs}, HIRType::makeVoid());
                lastValue_ = rhs;
                return;
            }
        }

        builder_.createSetPropStatic(obj, propAccess->name, rhs);
        lastValue_ = rhs;
        return;
    }

    // Handle element access assignment
    auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get());
    if (elemAccess) {
        auto obj = lowerExpression(elemAccess->expression.get());
        auto idx = lowerExpression(elemAccess->argumentExpression.get());
        builder_.createSetElem(obj, idx, rhs);
        lastValue_ = rhs;
        return;
    }

    lastValue_ = rhs;
}

void ASTToHIR::visitCallExpression(ast::CallExpression* node) {
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Handle super() call - calls parent class constructor
    auto* superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get());
    if (superExpr && currentClass_ && currentClass_->baseClass) {
        if (currentClass_->baseClass->constructor) {
            // Base class has explicit constructor - call it with [this, ...args]
            std::vector<std::shared_ptr<HIRValue>> ctorArgs;
            auto thisVal = lookupVariable("this");
            if (thisVal) {
                ctorArgs.push_back(thisVal);
            } else {
                ctorArgs.push_back(builder_.createConstNull());
            }
            for (auto& arg : args) {
                ctorArgs.push_back(arg);
            }
            builder_.createCall(currentClass_->baseClass->constructor->name, ctorArgs, HIRType::makeVoid());
        }
        // If base class has no explicit constructor (e.g., abstract class),
        // super() is a no-op - just continue with the derived class constructor
        lastValue_ = builder_.createConstUndefined();
        return;
    }

    // Handle method call
    auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get());
    if (propAccess) {
        // Check if we can use a direct call for method invocation

        // Case 1: Method call on 'this' - we know the class statically
        auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_) {
            // Look up the method in the current class
            auto it = currentClass_->methods.find(propAccess->name);
            if (it != currentClass_->methods.end()) {
                HIRFunction* method = it->second;
                // Build args: [this, ...args]
                std::vector<std::shared_ptr<HIRValue>> methodArgs;
                auto thisVal = lookupVariable("this");
                if (thisVal) {
                    methodArgs.push_back(thisVal);
                } else {
                    methodArgs.push_back(builder_.createConstNull());
                }
                for (auto& arg : args) {
                    methodArgs.push_back(arg);
                }
                // Direct call to the method function
                lastValue_ = builder_.createCall(method->name, methodArgs, method->returnType);
                return;
            }
        }

        // Case 2: Check if object has a known class type from inference
        std::string className;
        if (propAccess->expression->inferredType) {
            auto& type = propAccess->expression->inferredType;
            if (type->kind == ts::TypeKind::Class) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(type);
                if (classType) {
                    className = classType->name;
                }
            }
        }

        if (!className.empty()) {
            // Look up the class and search up the inheritance chain
            for (auto& cls : module_->classes) {
                if (cls->name == className) {
                    // Search in this class and all base classes
                    HIRClass* searchClass = cls.get();
                    while (searchClass) {
                        auto it = searchClass->methods.find(propAccess->name);
                        if (it != searchClass->methods.end()) {
                            HIRFunction* method = it->second;
                            // Build args: [obj, ...args]
                            auto obj = lowerExpression(propAccess->expression.get());
                            std::vector<std::shared_ptr<HIRValue>> methodArgs;
                            methodArgs.push_back(obj);
                            for (auto& arg : args) {
                                methodArgs.push_back(arg);
                            }
                            lastValue_ = builder_.createCall(method->name, methodArgs, method->returnType);
                            return;
                        }
                        // Move to base class
                        searchClass = searchClass->baseClass;
                    }
                    break;
                }
            }
        }

        // Case 3: Static method call - ClassName.methodName(...)
        auto* classNameIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (classNameIdent) {
            // Check if this is a class name
            for (auto& cls : module_->classes) {
                if (cls->name == classNameIdent->name) {
                    // Check for static method
                    auto it = cls->staticMethods.find(propAccess->name);
                    if (it != cls->staticMethods.end()) {
                        HIRFunction* method = it->second;
                        // Static methods don't need 'this' parameter
                        lastValue_ = builder_.createCall(method->name, args, method->returnType);
                        return;
                    }
                    break;
                }
            }

            // Case 4: Node.js builtin module method call - path.basename(...), fs.readFileSync(...), etc.
            // Check against ExtensionRegistry instead of hardcoded list
            auto& registry = ext::ExtensionRegistry::instance();
            if (registry.isRegisteredModule(classNameIdent->name) || registry.isRegisteredObject(classNameIdent->name)) {
                // Emit direct call to ts_<module>_<method>
                std::string runtimeFunc = "ts_" + classNameIdent->name + "_" + propAccess->name;
                lastValue_ = builder_.createCall(runtimeFunc, args, HIRType::makeAny());
                return;
            }
        }

        // Fallback: Dynamic method call
        auto obj = lowerExpression(propAccess->expression.get());
        lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
        return;
    }

    // Handle direct function call
    auto* ident = dynamic_cast<ast::Identifier*>(node->callee.get());
    if (ident) {
        // Check if this is a local variable (might be a closure)
        auto* info = lookupVariableInfo(ident->name);
        if (info) {
            // It's a local variable - load the function pointer and call indirectly
            std::shared_ptr<HIRValue> funcPtr;
            std::shared_ptr<HIRType> funcType;
            if (info->isAlloca && info->elemType) {
                funcPtr = builder_.createLoad(info->elemType, info->value);
                funcType = info->elemType;
            } else {
                funcPtr = info->value;
                funcType = info->value->type;
            }
            // Get return type from function type if available
            std::shared_ptr<HIRType> resultType = HIRType::makeAny();
            if (funcType && funcType->kind == HIRTypeKind::Function && funcType->returnType) {
                resultType = funcType->returnType;
            }
            lastValue_ = builder_.createCallIndirect(funcPtr, args, resultType);
            return;
        }
        // Handle builtin globals that are called as functions
        if (ident->name == "Symbol") {
            // Symbol(description?) creates a unique symbol
            std::shared_ptr<HIRValue> desc;
            if (!args.empty()) {
                desc = args[0];
            } else {
                desc = builder_.createConstNull();
            }
            lastValue_ = builder_.createCall("ts_symbol_create", {desc}, HIRType::makeSymbol());
            return;
        }

        if (ident->name == "BigInt") {
            // BigInt(value) converts value to BigInt
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_bigint_from_value", {args[0]}, HIRType::makeBigInt());
            } else {
                lastValue_ = builder_.createConstNull();
            }
            return;
        }

        if (ident->name == "Boolean") {
            // Boolean(value) converts to boolean using JavaScript truthiness
            if (!args.empty()) {
                // ts_value_to_bool expects a boxed TsValue*, so we need to box the argument
                auto arg = args[0];
                std::shared_ptr<HIRValue> boxed;
                if (arg->type) {
                    switch (arg->type->kind) {
                        case HIRTypeKind::Int64:
                            boxed = builder_.createBoxInt(arg);
                            break;
                        case HIRTypeKind::Float64:
                            boxed = builder_.createBoxFloat(arg);
                            break;
                        case HIRTypeKind::Bool:
                            boxed = builder_.createBoxBool(arg);
                            break;
                        case HIRTypeKind::String:
                            boxed = builder_.createBoxString(arg);
                            break;
                        case HIRTypeKind::Any:
                            // Already boxed
                            boxed = arg;
                            break;
                        default:
                            // For objects, arrays, etc. - box as object
                            boxed = builder_.createBoxObject(arg);
                            break;
                    }
                } else {
                    // Unknown type, assume it needs boxing as object
                    boxed = builder_.createBoxObject(arg);
                }
                lastValue_ = builder_.createCall("ts_value_to_bool", {boxed}, HIRType::makeBool());
            } else {
                lastValue_ = builder_.createConstBool(false);
            }
            return;
        }

        if (ident->name == "Number") {
            // Number(value) converts to number
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_to_number", {args[0]}, HIRType::makeFloat64());
            } else {
                lastValue_ = builder_.createConstFloat(0.0);
            }
            return;
        }

        if (ident->name == "String") {
            // String(value) converts to string
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_to_string", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createCall("ts_string_create", {builder_.createConstNull()}, HIRType::makeString());
            }
            return;
        }

        // Not a local variable - direct function call
        // First check specializations for rest parameters - this info is available
        // even before the HIR functions are created
        HIRFunction* targetFunc = nullptr;
        std::string callName;
        bool hasRestParam = false;
        size_t restParamIndex = 0;
        std::shared_ptr<HIRType> restElemType = HIRType::makeAny();

        // Track if we found default parameters and should use the specialization's name
        bool hasDefaultParams = false;
        size_t requiredParamCount = 0;
        size_t totalParamCount = 0;
        ast::FunctionDeclaration* foundFuncNode = nullptr;

        // Look up specialization by original function name to check for rest params and default params
        if (specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == ident->name) {
                    // Found a specialization for this function
                    if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                        foundFuncNode = funcNode;
                        totalParamCount = funcNode->parameters.size();

                        // Check if function has rest parameter or default parameters
                        for (size_t i = 0; i < funcNode->parameters.size(); ++i) {
                            if (funcNode->parameters[i]->isRest) {
                                hasRestParam = true;
                                restParamIndex = i;
                                // Get the element type from the parameter type annotation
                                // e.g., "...numbers: number[]" -> element type is number (Float64)
                                std::string paramType = funcNode->parameters[i]->type;
                                if (!paramType.empty()) {
                                    // Extract element type from array type (e.g., "number[]" -> "number")
                                    if (paramType.size() > 2 && paramType.substr(paramType.size() - 2) == "[]") {
                                        std::string elemTypeStr = paramType.substr(0, paramType.size() - 2);
                                        restElemType = convertTypeFromString(elemTypeStr);
                                    }
                                }
                                // Use this specialization's name - it already has the correct mangling
                                callName = spec.specializedName;
                                break;
                            }
                            // Check for default parameter
                            if (funcNode->parameters[i]->initializer) {
                                hasDefaultParams = true;
                            } else {
                                // Count required params (params before any with defaults)
                                if (!hasDefaultParams) {
                                    requiredParamCount = i + 1;
                                }
                            }
                        }

                        // If we have fewer args than total params but enough for required params,
                        // and there are default params, use this specialization's name
                        if (!hasRestParam && hasDefaultParams &&
                            args.size() < totalParamCount && args.size() >= requiredParamCount) {
                            callName = spec.specializedName;
                            // Pad args with undefined for missing default params
                            for (size_t i = args.size(); i < totalParamCount; ++i) {
                                args.push_back(builder_.createConstUndefined());
                            }
                        }
                    }
                    if (hasRestParam || (hasDefaultParams && !callName.empty())) break;
                }
            }
        }

        // If we didn't find a rest-parameter function, compute the mangled name based on argument types
        if (!hasRestParam) {
            std::vector<std::shared_ptr<ts::Type>> argTypes;
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType ? arg->inferredType : std::make_shared<ts::Type>(ts::TypeKind::Any));
            }
            std::string mangledName = Monomorphizer::generateMangledName(ident->name, argTypes, node->resolvedTypeArguments);
            callName = mangledName;

            // Look up the function - try mangled name first, then original name
            for (auto& f : module_->functions) {
                if (f->name == mangledName) {
                    targetFunc = f.get();
                    break;
                }
            }
            // If not found with mangled name, try original name (for runtime functions etc.)
            if (!targetFunc) {
                for (auto& f : module_->functions) {
                    if (f->name == ident->name) {
                        targetFunc = f.get();
                        callName = ident->name;  // Use original name
                        break;
                    }
                }
            }
        }
        // If still not found, determine if this is a runtime function or user function
        if (!targetFunc) {
            // Runtime functions start with "ts_" - use original name
            // User functions should use the mangled name
            if (ident->name.substr(0, 3) == "ts_" ||
                ident->name == "console" ||
                ident->name == "Math" ||
                ident->name == "JSON" ||
                ident->name == "parseInt" ||
                ident->name == "parseFloat" ||
                ident->name == "isNaN" ||
                ident->name == "isFinite") {
                callName = ident->name;  // Keep original name for runtime functions
            }
            // Otherwise keep the mangled name (already set above)
        }

        // Handle rest parameters: package excess arguments into an array
        // We use the hasRestParam flag computed from specializations_ lookup above
        if (hasRestParam) {
            std::vector<std::shared_ptr<HIRValue>> newArgs;

            // Add arguments before the rest parameter
            for (size_t i = 0; i < restParamIndex && i < args.size(); ++i) {
                newArgs.push_back(args[i]);
            }

            // Pad with undefined for missing non-rest arguments
            while (newArgs.size() < restParamIndex) {
                newArgs.push_back(builder_.createConstUndefined());
            }

            // Create the rest array
            size_t restArgsCount = (args.size() > restParamIndex) ? args.size() - restParamIndex : 0;
            auto lenVal = builder_.createConstInt(static_cast<int64_t>(restArgsCount));
            auto restArray = builder_.createNewArrayBoxed(lenVal, restElemType);

            // Add elements to the rest array
            for (size_t i = restParamIndex; i < args.size(); ++i) {
                auto idxVal = builder_.createConstInt(static_cast<int64_t>(i - restParamIndex));
                builder_.createSetElem(restArray, idxVal, args[i]);
            }

            newArgs.push_back(restArray);
            args = std::move(newArgs);
        } else if (targetFunc) {
            // If we found the function and have fewer args than params, pad with undefined
            if (args.size() < targetFunc->params.size()) {
                for (size_t i = args.size(); i < targetFunc->params.size(); ++i) {
                    args.push_back(builder_.createConstUndefined());
                }
            }
        }

        // Determine return type from target function if available
        auto returnType = (targetFunc && targetFunc->returnType) ? targetFunc->returnType : HIRType::makeAny();
        lastValue_ = builder_.createCall(callName, args, returnType);
        return;
    }

    // Generic case - callee is an expression (IIFE, function expression, etc.)
    // Lower the callee expression to get the function/closure pointer
    auto calleeVal = lowerExpression(node->callee.get());

    // Determine return type from the callee's function type if available
    std::shared_ptr<HIRType> resultType = HIRType::makeAny();
    if (calleeVal && calleeVal->type && calleeVal->type->kind == HIRTypeKind::Function && calleeVal->type->returnType) {
        resultType = calleeVal->type->returnType;
    }

    // Call the function indirectly
    lastValue_ = builder_.createCallIndirect(calleeVal, args, resultType);
}

void ASTToHIR::visitNewExpression(ast::NewExpression* node) {
    // Get constructor/class name
    auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
    std::string className = "Object";
    if (ident) {
        // First check if this is a variable pointing to a class expression
        auto it = variableToClassName_.find(ident->name);
        if (it != variableToClassName_.end()) {
            className = it->second;  // Use the actual generated class name
        } else {
            className = ident->name;
        }
    }

    // Handle built-in Array class
    if (className == "Array") {
        // new Array() or new Array(length) or new Array(elem1, elem2, ...)
        // Try to infer element type from type parameter
        std::shared_ptr<HIRType> elemType = HIRType::makeAny();
        if (node->inferredType && node->inferredType->kind == ts::TypeKind::Array) {
            auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->inferredType);
            if (arrayType->elementType) {
                elemType = convertType(arrayType->elementType);
            }
        }

        if (node->arguments.empty()) {
            // new Array() - create empty array
            auto zero = builder_.createConstInt(0);
            lastValue_ = builder_.createNewArrayBoxed(zero, elemType);
        } else if (node->arguments.size() == 1) {
            // Check if single argument is a number (length) or element
            auto& arg = node->arguments[0];
            bool isNumericArg = arg->inferredType &&
                (arg->inferredType->kind == ts::TypeKind::Double ||
                 arg->inferredType->kind == ts::TypeKind::Int);
            if (isNumericArg) {
                // new Array(length) - create array with capacity
                auto lenVal = lowerExpression(arg.get());
                lastValue_ = builder_.createNewArrayBoxed(lenVal, elemType);
            } else {
                // new Array(elem) - create array with single element
                auto zero = builder_.createConstInt(0);
                auto arr = builder_.createNewArrayBoxed(zero, elemType);
                auto elemVal = lowerExpression(arg.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
                lastValue_ = arr;
            }
        } else {
            // new Array(elem1, elem2, ...) - create array with elements
            auto zero = builder_.createConstInt(0);
            auto arr = builder_.createNewArrayBoxed(zero, elemType);
            for (auto& arg : node->arguments) {
                auto elemVal = lowerExpression(arg.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
            }
            lastValue_ = arr;
        }
        return;
    }

    // Handle built-in Map class
    if (className == "Map") {
        lastValue_ = builder_.createCall("ts_map_create", {}, HIRType::makeMap());
        return;
    }

    // Handle built-in Set class
    if (className == "Set") {
        lastValue_ = builder_.createCall("ts_set_create", {}, HIRType::makeSet());
        return;
    }

    // Handle built-in Error classes
    if (className == "Error" || className == "TypeError" || className == "RangeError" ||
        className == "ReferenceError" || className == "SyntaxError" || className == "URIError" ||
        className == "EvalError" || className == "AggregateError") {
        // new Error(message) or new Error(message, { cause: ... })
        std::shared_ptr<HIRValue> message;
        if (!node->arguments.empty()) {
            message = lowerExpression(node->arguments[0].get());
        } else {
            // Create empty string
            message = builder_.createConstString("");
        }

        // Call ts_error_create (returns already-boxed TsValue*)
        if (node->arguments.size() >= 2) {
            // ES2022: Error with options { cause: ... }
            auto options = lowerExpression(node->arguments[1].get());
            lastValue_ = builder_.createCall("ts_error_create_with_options", {message, options}, HIRType::makeAny());
        } else {
            lastValue_ = builder_.createCall("ts_error_create", {message}, HIRType::makeAny());
        }
        return;
    }

    // Lower constructor arguments
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Look up the class
    HIRClass* hirClass = nullptr;
    for (auto& cls : module_->classes) {
        if (cls->name == className) {
            hirClass = cls.get();
            break;
        }
    }

    // Create new object with the correct type
    std::shared_ptr<HIRValue> newObj;
    if (hirClass && hirClass->shape) {
        // Use the class's shape to create a properly typed object
        newObj = builder_.createNewObject(hirClass->shape.get());
    } else {
        // Fallback to dynamic object (for built-in or unknown classes)
        newObj = builder_.createNewObjectDynamic();
    }

    if (hirClass && hirClass->constructor) {
        // Build constructor call args: [this, ...args]
        std::vector<std::shared_ptr<HIRValue>> ctorArgs;
        ctorArgs.push_back(newObj);  // 'this' is the new object
        for (auto& arg : args) {
            ctorArgs.push_back(arg);
        }

        // Call the constructor
        builder_.createCall(hirClass->constructor->name, ctorArgs, HIRType::makeVoid());
    }

    // The result is the new object
    lastValue_ = newObj;
}

void ASTToHIR::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    // Try to infer element type from the array's inferred type
    std::shared_ptr<HIRType> elemType = HIRType::makeAny();
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->inferredType);
        if (arrayType->elementType) {
            elemType = convertType(arrayType->elementType);
        }
    }

    // Check if we have any spread elements - if so, we need dynamic approach
    bool hasSpread = false;
    for (auto& elem : node->elements) {
        if (dynamic_cast<ast::SpreadElement*>(elem.get())) {
            hasSpread = true;
            break;
        }
    }

    if (hasSpread) {
        // With spread elements, use ts_array_create and dynamic push/concat
        auto arr = builder_.createCall("ts_array_create", {}, HIRType::makeArray(elemType, false));

        for (auto& elem : node->elements) {
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(elem.get())) {
                // Spread element: concatenate the spread array
                // ts_array_concat returns a NEW array, so we must capture it
                auto spreadArr = lowerExpression(spread->expression.get());
                arr = builder_.createCall("ts_array_concat", {arr, spreadArr}, HIRType::makeArray(elemType, false));
            } else {
                // Regular element: push it
                auto elemVal = lowerExpression(elem.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
            }
        }

        lastValue_ = arr;
    } else {
        // No spread elements - use efficient pre-allocated array
        auto lenVal = builder_.createConstInt(static_cast<int64_t>(node->elements.size()));
        auto arr = builder_.createNewArrayBoxed(lenVal, elemType);

        int64_t idx = 0;
        for (auto& elem : node->elements) {
            auto elemVal = lowerExpression(elem.get());
            auto idxVal = builder_.createConstInt(idx++);
            builder_.createSetElem(arr, idxVal, elemVal);
        }

        lastValue_ = arr;
    }
}

void ASTToHIR::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    // Check for enum reverse mapping: EnumName[numericValue]
    auto* classNameIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (classNameIdent) {
        auto enumReverseIt = enumReverseMap_.find(classNameIdent->name);
        if (enumReverseIt != enumReverseMap_.end()) {
            // This is an enum reverse mapping access
            if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(node->argumentExpression.get())) {
                // Constant index - look up at compile time
                int64_t idx = static_cast<int64_t>(numLit->value);
                auto memberIt = enumReverseIt->second.find(idx);
                if (memberIt != enumReverseIt->second.end()) {
                    lastValue_ = builder_.createConstString(memberIt->second);
                    return;
                }
            }
            // Dynamic index - need runtime lookup (TODO: generate runtime object for dynamic access)
            // For now, fall through to dynamic access
        }
    }

    auto obj = lowerExpression(node->expression.get());

    // Handle optional chaining: obj?.[idx]
    if (node->isOptional) {
        // Check if obj is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {obj}, HIRType::makeBool());

        // Create undefined value before branching (so it's in the current block)
        auto undef = builder_.createConstUndefined();

        // Create blocks for conditional access (with unique names)
        int blockId = blockCounter_++;
        auto* accessBlock = builder_.createBlock("opt_access_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("opt_merge_" + std::to_string(blockId));

        // Branch based on nullish check
        auto* currentBlock = builder_.getInsertBlock();
        builder_.createCondBranch(isNullish, mergeBlock, accessBlock);

        // Access block: perform the element access
        builder_.setInsertPoint(accessBlock);
        auto idx = lowerExpression(node->argumentExpression.get());
        auto accessResult = builder_.createGetElem(obj, idx);
        auto* finalAccessBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge block: phi node to select result
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(undef, currentBlock));
        phiIncoming.push_back(std::make_pair(accessResult, finalAccessBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    auto idx = lowerExpression(node->argumentExpression.get());
    lastValue_ = builder_.createGetElem(obj, idx);
}

void ASTToHIR::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    // Check for static property access: ClassName.propertyName
    auto* classNameIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (classNameIdent) {
        // Check for enum member access: EnumName.MemberName
        auto enumIt = enumValues_.find(classNameIdent->name);
        if (enumIt != enumValues_.end()) {
            auto memberIt = enumIt->second.find(node->name);
            if (memberIt != enumIt->second.end()) {
                const EnumValue& ev = memberIt->second;
                if (ev.isString) {
                    lastValue_ = builder_.createConstString(ev.strValue);
                } else {
                    // Use float64 for consistency with JS number semantics
                    lastValue_ = builder_.createConstFloat(static_cast<double>(ev.numValue));
                }
                return;
            }
        }

        for (auto& cls : module_->classes) {
            if (cls->name == classNameIdent->name) {
                // Check if this is a static property
                std::string globalName = cls->name + "_static_" + node->name;
                auto it = staticPropertyGlobals_.find(globalName);
                if (it != staticPropertyGlobals_.end()) {
                    // Load from the static property global
                    auto globalPtr = it->second.first;
                    auto propType = it->second.second;
                    lastValue_ = builder_.createLoad(propType, globalPtr);
                    return;
                }
                break;
            }
        }
    }

    auto obj = lowerExpression(node->expression.get());

    // Determine the property type - check if this is 'this' access in a class context
    std::shared_ptr<HIRType> propType = HIRType::makeAny();

    // Special handling for built-in type properties
    if (node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;

        // Array.length returns a number - call ts_array_length directly
        if (exprType->kind == ts::TypeKind::Array && node->name == "length") {
            lastValue_ = builder_.createCall("ts_array_length", {obj}, HIRType::makeInt64());
            return;
        }
        // String.length returns a number - call ts_string_length directly
        else if (exprType->kind == ts::TypeKind::String && node->name == "length") {
            lastValue_ = builder_.createCall("ts_string_length", {obj}, HIRType::makeInt64());
            return;
        }
    }

    if (currentClass_) {
        // Check if the expression is 'this'
        auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_->shape) {
            // Look up the property type from the class shape
            auto typeIt = currentClass_->shape->propertyTypes.find(node->name);
            if (typeIt != currentClass_->shape->propertyTypes.end()) {
                propType = typeIt->second;
            }
        }
    }

    // Check for getter: look up the class type and see if it has __getter_<propName>
    HIRClass* targetClass = nullptr;

    // First, check if expression has an inferred class type
    if (node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;
        if (exprType->kind == ts::TypeKind::Class) {
            auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
            if (classType) {
                // Find the HIRClass by name
                for (auto& cls : module_->classes) {
                    if (cls->name == classType->name) {
                        targetClass = cls.get();
                        break;
                    }
                }
            }
        }
    }

    // If accessing 'this', use currentClass_
    if (!targetClass) {
        auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_) {
            targetClass = currentClass_;
        }
    }

    // Check if the target class has a getter for this property
    if (targetClass) {
        std::string getterKey = "__getter_" + node->name;
        auto getterIt = targetClass->methods.find(getterKey);
        if (getterIt != targetClass->methods.end()) {
            // Found a getter - call it instead of direct property access
            HIRFunction* getterFunc = getterIt->second;
            auto returnType = getterFunc->returnType ? getterFunc->returnType : HIRType::makeAny();
            lastValue_ = builder_.createCall(getterFunc->name, {obj}, returnType);
            return;
        }
    }

    // Handle optional chaining: obj?.prop
    if (node->isOptional) {
        // Check if obj is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {obj}, HIRType::makeBool());

        // Create undefined value before branching (so it's in the current block)
        auto undef = builder_.createConstUndefined();

        // Create blocks for conditional access (with unique names)
        int blockId = blockCounter_++;
        auto* accessBlock = builder_.createBlock("opt_access_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("opt_merge_" + std::to_string(blockId));

        // Branch based on nullish check
        auto* currentBlock = builder_.getInsertBlock();
        builder_.createCondBranch(isNullish, mergeBlock, accessBlock);

        // Access block: perform the property access
        builder_.setInsertPoint(accessBlock);
        auto accessResult = builder_.createGetPropStatic(obj, node->name, propType);
        auto* finalAccessBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge block: phi node to select result
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(undef, currentBlock));
        phiIncoming.push_back(std::make_pair(accessResult, finalAccessBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    lastValue_ = builder_.createGetPropStatic(obj, node->name, propType);
}

void ASTToHIR::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    auto obj = builder_.createNewObjectDynamic();

    for (auto& prop : node->properties) {
        // Handle spread element: {...other}
        if (auto* spread = dynamic_cast<ast::SpreadElement*>(prop.get())) {
            auto spreadObj = lowerExpression(spread->expression.get());
            // Use ts_object_assign to copy properties from spreadObj to obj
            builder_.createCall("ts_object_assign", {obj, spreadObj}, HIRType::makeAny());
            continue;
        }

        // Handle MethodDefinition (including getters/setters) specially
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            // Create a function for the method
            auto funcValue = lowerMethodDefinitionToFunction(method);

            // Determine the property key
            std::string keyName;
            if (auto* id = dynamic_cast<ast::Identifier*>(method->nameNode.get())) {
                if (method->isGetter) {
                    keyName = "__getter_" + id->name;
                } else if (method->isSetter) {
                    keyName = "__setter_" + id->name;
                } else {
                    keyName = id->name;
                }
            } else if (!method->name.empty()) {
                if (method->isGetter) {
                    keyName = "__getter_" + method->name;
                } else if (method->isSetter) {
                    keyName = "__setter_" + method->name;
                } else {
                    keyName = method->name;
                }
            }

            if (!keyName.empty() && funcValue) {
                builder_.createSetPropStatic(obj, keyName, funcValue);
            }
        } else {
            // Save the object before visiting property (which may overwrite lastValue_)
            lastValue_ = obj;
            prop->accept(this);
        }
    }

    // Ensure lastValue_ is the object after all properties are set
    lastValue_ = obj;
}

void ASTToHIR::visitPropertyAssignment(ast::PropertyAssignment* node) {
    // Save the object before lowerExpression overwrites lastValue_
    auto obj = lastValue_;

    auto val = lowerExpression(node->initializer.get());

    // PropertyAssignment has name (string) directly
    std::string propName = node->name;

    if (!propName.empty() && obj) {
        builder_.createSetPropStatic(obj, propName, val);
    }

    // Restore lastValue_ to the object for any subsequent properties
    lastValue_ = obj;
}

void ASTToHIR::visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) {
    // Save the object before any potential modification to lastValue_
    auto obj = lastValue_;

    auto val = lookupVariable(node->name);
    if (!val) {
        // Variable not found - check if it's a function name in the module
        for (const auto& func : module_->functions) {
            if (func->name == node->name) {
                // Found a function with this name - load it as a function value
                auto funcType = HIRType::makeFunction();
                funcType->returnType = func->returnType;
                for (const auto& param : func->params) {
                    funcType->paramTypes.push_back(param.second);
                }
                val = builder_.createLoadFunction(node->name, funcType);
                break;
            }
        }

        // Also check specializations - functions might be pending compilation
        if (!val && specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == node->name || spec.specializedName == node->name) {
                    // Found a function declaration - use LoadFunction
                    auto funcType = HIRType::makeFunction();
                    if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                        if (!funcNode->returnType.empty()) {
                            funcType->returnType = convertTypeFromString(funcNode->returnType);
                        }
                        for (const auto& param : funcNode->parameters) {
                            auto paramType = param->type.empty()
                                ? HIRType::makeAny()
                                : convertTypeFromString(param->type);
                            funcType->paramTypes.push_back(paramType);
                        }
                    }
                    val = builder_.createLoadFunction(spec.specializedName, funcType);
                    break;
                }
            }
        }

        // If still not found, create undefined
        if (!val) {
            val = createValue(HIRType::makeAny());
            builder_.createConstUndefined(val);
        }
    }

    if (obj) {
        builder_.createSetPropStatic(obj, node->name, val);
    }

    // Restore lastValue_ to the object for any subsequent properties
    lastValue_ = obj;
}

void ASTToHIR::visitComputedPropertyName(ast::ComputedPropertyName* node) {
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitMethodDefinition(ast::MethodDefinition* node) {
    // Methods are handled during class lowering
}

void ASTToHIR::visitStaticBlock(ast::StaticBlock* node) {
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }
}

void ASTToHIR::visitIdentifier(ast::Identifier* node) {
    // Handle 'this' keyword specially
    if (node->name == "this") {
        // Look up 'this' in the variable scope - it's set as a parameter in method bodies
        lastValue_ = lookupVariable("this");
        if (lastValue_) {
            return;
        }
        // If not found, return null (e.g., in static context)
        lastValue_ = builder_.createConstNull();
        return;
    }

    // Check if this is a captured variable from an outer function
    size_t scopeIndex = 0;
    if (currentFunction_ && isCapturedVariable(node->name, &scopeIndex)) {
        // Look up the variable info to get its type
        auto* info = lookupVariableInfo(node->name);
        if (info) {
            auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
            // Register this capture for the current function
            registerCapture(node->name, type, scopeIndex);
            // Mark the function as having closures
            currentFunction_->hasClosure = true;
            // Use LoadCapture for captured variables
            lastValue_ = builder_.createLoadCapture(node->name, type);
            return;
        }
    }

    // Check for local/parameter variables
    lastValue_ = lookupVariable(node->name);
    if (lastValue_) {
        return;
    }

    // Handle special constants first (these are always hardcoded)
    if (node->name == "undefined") {
        lastValue_ = builder_.createConstUndefined();
        return;
    }
    if (node->name == "NaN") {
        lastValue_ = builder_.createConstFloat(std::nan(""));
        return;
    }
    if (node->name == "Infinity") {
        lastValue_ = builder_.createConstFloat(std::numeric_limits<double>::infinity());
        return;
    }

    // Check ExtensionRegistry for registered objects/modules/globals
    // These include: console, Math, JSON, Object, Array, String, Number, Boolean,
    // Date, RegExp, Promise, Error, Buffer, process, global, globalThis,
    // and Node.js modules like path, fs, os, url, util, crypto, http, https, net, etc.
    auto& registry = ext::ExtensionRegistry::instance();
    if (registry.isRegisteredGlobalOrModule(node->name)) {
        // Emit LoadGlobal for global objects
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
    }

    // Fallback: Check for known JavaScript built-in objects not yet in extension files
    // This maintains backwards compatibility while migrating to registry-based lookups
    static const std::set<std::string> builtinObjects = {
        "Math", "JSON", "Object", "Array", "String", "Number",
        "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
        "process", "global", "globalThis"
    };
    if (builtinObjects.count(node->name)) {
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
    }

    // Check if this is a function name in the module
    // Functions are declared at module level and can be referenced as values
    for (const auto& func : module_->functions) {
        if (func->name == node->name) {
            // Found a function with this name - load it as a function value
            auto funcType = HIRType::makeFunction();
            funcType->returnType = func->returnType;
            for (const auto& param : func->params) {
                funcType->paramTypes.push_back(param.second);
            }
            lastValue_ = builder_.createLoadFunction(node->name, funcType);
            return;
        }
    }

    // Also check specializations - functions might be pending compilation
    if (specializations_) {
        for (const auto& spec : *specializations_) {
            if (spec.originalName == node->name || spec.specializedName == node->name) {
                // Found a function declaration - use LoadFunction
                auto funcType = HIRType::makeFunction();
                if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                    if (!funcNode->returnType.empty()) {
                        funcType->returnType = convertTypeFromString(funcNode->returnType);
                    }
                    for (const auto& param : funcNode->parameters) {
                        auto paramType = param->type.empty()
                            ? HIRType::makeAny()
                            : convertTypeFromString(param->type);
                        funcType->paramTypes.push_back(paramType);
                    }
                }
                lastValue_ = builder_.createLoadFunction(spec.specializedName, funcType);
                return;
            }
        }
    }

    // Unknown variable - create undefined
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitSuperExpression(ast::SuperExpression* node) {
    // TODO: Proper super handling
    lastValue_ = createValue(HIRType::makeObject());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitStringLiteral(ast::StringLiteral* node) {
    lastValue_ = builder_.createConstString(node->value);
}

void ASTToHIR::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    // Create a RegExp object from the literal text (e.g., "/pattern/flags")
    // The runtime function ts_regexp_from_literal parses the literal and creates the RegExp
    auto literalStr = builder_.createConstString(node->text);
    lastValue_ = builder_.createCall("ts_regexp_from_literal", {literalStr}, HIRType::makeObject());
}

void ASTToHIR::visitNumericLiteral(ast::NumericLiteral* node) {
    // In TypeScript/JavaScript, all numbers are IEEE 754 double-precision floats
    lastValue_ = builder_.createConstFloat(node->value);
}

void ASTToHIR::visitBigIntLiteral(ast::BigIntLiteral* node) {
    // Create a BigInt from the literal string value (e.g., "123n" -> "123")
    // The value in the AST includes the 'n' suffix, so we need to strip it
    std::string valueStr = node->value;
    if (!valueStr.empty() && valueStr.back() == 'n') {
        valueStr.pop_back();
    }

    // Create the string constant for the BigInt value
    auto strVal = builder_.createConstString(valueStr);

    // Call ts_bigint_create_str with base 10
    auto radix = builder_.createConstInt(10);
    lastValue_ = builder_.createCall("ts_bigint_create_str", {strVal, radix}, HIRType::makeObject());
}

void ASTToHIR::visitBooleanLiteral(ast::BooleanLiteral* node) {
    lastValue_ = builder_.createConstBool(node->value);
}

void ASTToHIR::visitNullLiteral(ast::NullLiteral* node) {
    lastValue_ = builder_.createConstNull();
}

void ASTToHIR::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    lastValue_ = builder_.createConstUndefined();
}

void ASTToHIR::visitAwaitExpression(ast::AwaitExpression* node) {
    if (node->expression) {
        // Lower the promise expression
        auto promise = lowerExpression(node->expression.get());
        // Create await instruction to wait for promise resolution
        lastValue_ = builder_.createAwait(promise);
    } else {
        // await with no expression returns undefined
        lastValue_ = builder_.createConstUndefined();
    }
}

void ASTToHIR::visitYieldExpression(ast::YieldExpression* node) {
    // Yield: yield value or yield* iterable
    // yield returns the value passed to next() when generator is resumed
    // yield* delegates to another generator/iterable

    if (node->isAsterisk) {
        // yield* iterable - delegate to another generator
        if (node->expression) {
            auto iterable = lowerExpression(node->expression.get());
            lastValue_ = builder_.createYieldStar(iterable);
        } else {
            // yield* with no expression - undefined behavior, yield undefined
            auto undef = builder_.createConstUndefined();
            lastValue_ = builder_.createYieldStar(undef);
        }
    } else {
        // Regular yield
        if (node->expression) {
            auto value = lowerExpression(node->expression.get());
            lastValue_ = builder_.createYield(value);
        } else {
            // yield with no expression yields undefined
            auto undef = builder_.createConstUndefined();
            lastValue_ = builder_.createYield(undef);
        }
    }
}

void ASTToHIR::visitDynamicImport(ast::DynamicImport* node) {
    // TODO: Dynamic import support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitArrowFunction(ast::ArrowFunction* node) {
    // Generate unique function name for the arrow function
    std::string funcName = "__arrow_fn_" + std::to_string(arrowFuncCounter_++);

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = false;  // Arrow functions can't be generators

    // Get function type info from inferred type if available
    std::shared_ptr<ts::FunctionType> tsFuncType = nullptr;
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Function) {
        tsFuncType = std::static_pointer_cast<ts::FunctionType>(node->inferredType);
    }

    // Add hidden __closure__ parameter as first parameter (for call_indirect compatibility)
    // call_indirect always passes the closure as the first argument
    func->params.push_back({"__closure__", HIRType::makePtr()});

    // Handle parameters - use inferred types from function type if available
    for (size_t i = 0; i < node->parameters.size(); ++i) {
        auto& param = node->parameters[i];
        std::shared_ptr<HIRType> paramType;

        // First try explicit type annotation
        if (!param->type.empty()) {
            paramType = convertTypeFromString(param->type);
        }
        // Then try inferred type from function signature
        else if (tsFuncType && i < tsFuncType->paramTypes.size() && tsFuncType->paramTypes[i]) {
            paramType = convertType(tsFuncType->paramTypes[i]);
        }
        // Finally fall back to Any
        else {
            paramType = HIRType::makeAny();
        }

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }
        func->params.push_back({paramName, paramType});
    }

    // Determine return type from inferred type or default to Any
    std::shared_ptr<HIRType> returnType = HIRType::makeAny();
    if (tsFuncType && tsFuncType->returnType) {
        returnType = convertType(tsFuncType->returnType);
    }
    func->returnType = returnType;

    // Save current context
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Register parameters in the scope
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
        defineVariable(paramName, paramValue);
    }
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Lower function body
    // The body can be either a BlockStatement or an Expression (implicit return)
    if (auto* blockStmt = dynamic_cast<ast::BlockStatement*>(node->body.get())) {
        // Block body - lower each statement
        for (auto& stmt : blockStmt->statements) {
            lowerStatement(stmt.get());
            // Stop processing after a terminator
            if (builder_.isBlockTerminated()) {
                break;
            }
        }
    } else if (auto* exprBody = dynamic_cast<ast::Expression*>(node->body.get())) {
        // Expression body - implicit return
        auto retVal = lowerExpression(exprBody);
        // If return type is void, don't return the value (just execute the expression for side effects)
        if (returnType->kind != HIRTypeKind::Void) {
            builder_.createReturn(retVal);
        }
    }

    // Add implicit return void if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use (after we restore context)
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Restore saved context
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Get the function pointer before adding to module
    HIRFunction* funcPtr = func.get();

    // Add function to module
    module_->functions.push_back(std::move(func));

    // Build function type for the closure (used for type inference at call sites)
    auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
    for (const auto& [paramName, paramType] : funcPtr->params) {
        closureFuncType->paramTypes.push_back(paramType);
    }
    closureFuncType->returnType = funcPtr->returnType;

    // Return either a closure or plain function pointer
    if (hasClosure && savedFunc) {
        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            // Check if this variable requires capture propagation (i.e., it's from
            // an outer function's scope, not the current function's scope)
            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                // Variable is in an outer function's scope - we need to propagate
                // the capture through the current function.
                // Register this capture for the current function too
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                // Also add to the function's captures list directly since it was
                // already finalized before we detected the propagation need
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                // Use LoadCapture to get the value
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                // Variable is directly accessible in the current function's scope
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    // Variable not found - shouldn't happen, but emit a placeholder
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }
        lastValue_ = builder_.createMakeClosure(funcName, captureValues, closureFuncType);

        // Mark each captured variable in the outer scope as "captured by nested"
        // so subsequent reads/writes in the outer function also use the cell
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info && !info->isCapturedByNested) {
                    info->isCapturedByNested = true;
                    info->closurePtr = lastValue_;
                    info->captureIndex = captureIdx;
                }
            }
            captureIdx++;
        }
    } else {
        // No captures, but still wrap in a closure for consistency with call_indirect
        // which always expects a TsClosure* (not a raw function pointer)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        lastValue_ = builder_.createMakeClosure(funcName, emptyCaptureValues, closureFuncType);
    }
}

void ASTToHIR::visitFunctionExpression(ast::FunctionExpression* node) {
    // Generate function name: use the node's name if available, otherwise generate one
    std::string funcName;
    if (!node->name.empty()) {
        // Named function expression - use the name but make it unique
        funcName = "__fn_expr_" + node->name + "_" + std::to_string(funcExprCounter_++);
    } else {
        // Anonymous function expression
        funcName = "__fn_expr_" + std::to_string(funcExprCounter_++);
    }

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;

    // Add hidden __closure__ parameter as first parameter (for call_indirect compatibility)
    // call_indirect always passes the closure as the first argument
    func->params.push_back({"__closure__", HIRType::makePtr()});

    // Handle parameters
    for (auto& param : node->parameters) {
        auto paramType = param->type.empty()
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }
        func->params.push_back({paramName, paramType});
    }

    // Determine return type from explicit return type or inferred type
    std::shared_ptr<HIRType> returnType = HIRType::makeAny();
    if (!node->returnType.empty()) {
        returnType = convertTypeFromString(node->returnType);
    } else if (node->inferredType && node->inferredType->kind == ts::TypeKind::Function) {
        auto funcType = std::static_pointer_cast<ts::FunctionType>(node->inferredType);
        if (funcType->returnType) {
            returnType = convertType(funcType->returnType);
        }
    }
    func->returnType = returnType;

    // Save current context
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Register parameters in the scope
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
        defineVariable(paramName, paramValue);
    }
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // If the function is named, make it available in its own scope (for recursion)
    if (!node->name.empty()) {
        auto fnPtr = std::make_shared<HIRValue>(func->nextValueId++, HIRType::makePtr(), node->name);
        defineVariable(node->name, fnPtr);
    }

    // Lower function body (vector of statements)
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }

    // Add implicit return undefined if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use (after we restore context)
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Restore saved context
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Get the function pointer before adding to module
    HIRFunction* funcPtr = func.get();

    // Add function to module
    module_->functions.push_back(std::move(func));

    // Build function type for the closure (used for type inference at call sites)
    auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
    for (const auto& [paramName, paramType] : funcPtr->params) {
        closureFuncType->paramTypes.push_back(paramType);
    }
    closureFuncType->returnType = funcPtr->returnType;

    // Return either a closure or plain function pointer
    if (hasClosure && savedFunc) {
        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            // Check if this variable requires capture propagation (i.e., it's from
            // an outer function's scope, not the current function's scope)
            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                // Variable is in an outer function's scope - we need to propagate
                // the capture through the current function.
                // Register this capture for the current function too
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                // Also add to the function's captures list directly since it was
                // already finalized before we detected the propagation need
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                // Use LoadCapture to get the value
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                // Variable is directly accessible in the current function's scope
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    // Variable not found - shouldn't happen, but emit a placeholder
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }
        lastValue_ = builder_.createMakeClosure(funcName, captureValues, closureFuncType);
    } else {
        // No captures, but still wrap in a closure for consistency with call_indirect
        // which always expects a TsClosure* (not a raw function pointer)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        lastValue_ = builder_.createMakeClosure(funcName, emptyCaptureValues, closureFuncType);
    }
}

std::shared_ptr<HIRValue> ASTToHIR::lowerMethodDefinitionToFunction(ast::MethodDefinition* node) {
    // Generate function name based on method name and type
    std::string prefix = node->isGetter ? "__getter_" : (node->isSetter ? "__setter_" : "__method_");
    std::string methodName = node->name;
    if (methodName.empty() && node->nameNode) {
        if (auto* id = dynamic_cast<ast::Identifier*>(node->nameNode.get())) {
            methodName = id->name;
        }
    }
    std::string funcName = prefix + methodName + "_" + std::to_string(methodCounter_++);

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;

    // Add implicit 'this' parameter for methods
    func->params.push_back({"this", HIRType::makeAny()});

    // Handle explicit parameters
    // For getters/setters, force params to be Any (TsValue*) since they will be called
    // through the runtime's dynamic dispatch which passes TsValue* arguments
    bool forceAnyParams = node->isGetter || node->isSetter;
    for (auto& param : node->parameters) {
        auto paramType = (forceAnyParams || param->type.empty())
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }
        func->params.push_back({paramName, paramType});
    }

    // Determine return type
    std::shared_ptr<HIRType> returnType = HIRType::makeAny();
    if (!node->returnType.empty()) {
        returnType = convertTypeFromString(node->returnType);
    } else if (node->inferredType && node->inferredType->kind == ts::TypeKind::Function) {
        auto funcType = std::static_pointer_cast<ts::FunctionType>(node->inferredType);
        if (funcType->returnType) {
            returnType = convertType(funcType->returnType);
        }
    }
    func->returnType = returnType;

    // Save current context
    HIRFunction* savedFunc = currentFunction_;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;

    currentFunction_ = func.get();
    clearPendingCaptures();

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope
    pushFunctionScope(func.get());

    // Register parameters in the scope (including 'this')
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
        defineVariable(paramName, paramValue);
    }
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Lower function body
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }

    // Add implicit return undefined if no terminator
    if (!hasTerminator()) {
        builder_.createReturnVoid();
    }

    // Copy pending captures to the function's captures list
    for (const auto& cap : pendingCaptures_) {
        func->captures.push_back({cap.name, cap.type});
    }
    bool hasClosure = !pendingCaptures_.empty();

    // Save the captures list for later use
    std::vector<std::pair<std::string, std::shared_ptr<HIRType>>> innerCaptures = func->captures;

    popScope();

    // Restore saved context
    currentFunction_ = savedFunc;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Get the function pointer before adding to module
    HIRFunction* funcPtr = func.get();

    // Add function to module
    module_->functions.push_back(std::move(func));

    // Build function type for the closure
    auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
    for (const auto& [paramName, paramType] : funcPtr->params) {
        closureFuncType->paramTypes.push_back(paramType);
    }
    closureFuncType->returnType = funcPtr->returnType;

    // Return either a closure or plain function pointer
    if (hasClosure && savedFunc) {
        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                registerCapture(capName, capType, scopeIndex);
                currentFunction_->hasClosure = true;
                bool alreadyInCaptures = false;
                for (const auto& existingCap : currentFunction_->captures) {
                    if (existingCap.first == capName) {
                        alreadyInCaptures = true;
                        break;
                    }
                }
                if (!alreadyInCaptures) {
                    currentFunction_->captures.push_back({capName, capType});
                }
                auto val = builder_.createLoadCapture(capName, capType);
                captureValues.push_back(val);
            } else {
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    std::shared_ptr<HIRValue> val;
                    if (info->isAlloca && info->elemType) {
                        val = builder_.createLoad(info->elemType, info->value);
                    } else {
                        val = info->value;
                    }
                    captureValues.push_back(val);
                } else {
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }
        return builder_.createMakeClosure(funcName, captureValues, closureFuncType);
    } else {
        // Pass the function type so SetPropStatic knows to box it as a function
        return builder_.createLoadFunction(funcName, closureFuncType);
    }
}

void ASTToHIR::visitTemplateExpression(ast::TemplateExpression* node) {
    // Start with the head string
    auto currentStr = builder_.createConstString(node->head);

    for (auto& span : node->spans) {
        // Lower the embedded expression
        auto exprValue = lowerExpression(span.expression.get());

        // Convert to string based on type
        std::shared_ptr<HIRValue> strValue;
        auto exprType = span.expression->inferredType;

        if (exprType && exprType->kind == TypeKind::Int) {
            // Integer to string conversion
            strValue = builder_.createCall("ts_string_from_int", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::Double) {
            // Double to string conversion
            strValue = builder_.createCall("ts_string_from_double", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::Boolean) {
            // Boolean to string conversion
            strValue = builder_.createCall("ts_string_from_bool", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::String) {
            // Already a string, use directly
            strValue = exprValue;
        } else {
            // For any/boxed types, use runtime coercion to string
            strValue = builder_.createCall("ts_string_from_value", {exprValue}, HIRType::makeString());
        }

        // Concatenate expression result
        currentStr = builder_.createStringConcat(currentStr, strValue);

        // Concatenate the literal part after the expression
        auto litValue = builder_.createConstString(span.literal);
        currentStr = builder_.createStringConcat(currentStr, litValue);
    }

    lastValue_ = currentStr;
}

void ASTToHIR::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    // Tagged template: tag`str${expr}str...`
    // Calls: tag(stringsArray, ...expressions)
    // stringsArray is an array of the literal parts with a 'raw' property

    if (!node->tag || !node->templateExpr) {
        lastValue_ = builder_.createConstUndefined();
        return;
    }

    // Lower the tag function
    auto tagFn = lowerExpression(node->tag.get());

    // Get template parts - templateExpr could be TemplateExpression or NoSubstitutionTemplateLiteral
    std::vector<std::string> stringParts;
    std::vector<std::shared_ptr<HIRValue>> expressions;

    auto* templateExpr = dynamic_cast<ast::TemplateExpression*>(node->templateExpr.get());
    if (templateExpr) {
        // Template with substitutions
        stringParts.push_back(templateExpr->head);

        for (const auto& span : templateExpr->spans) {
            if (span.expression) {
                expressions.push_back(lowerExpression(span.expression.get()));
            }
            stringParts.push_back(span.literal);
        }
    } else {
        // NoSubstitutionTemplateLiteral - just a single string
        auto* strLit = dynamic_cast<ast::StringLiteral*>(node->templateExpr.get());
        if (strLit) {
            stringParts.push_back(strLit->value);
        }
    }

    // Create the strings array with the proper elements
    auto arrayLen = builder_.createConstInt(static_cast<int64_t>(stringParts.size()));
    auto stringsArray = builder_.createNewArrayBoxed(arrayLen, HIRType::makeString());
    for (size_t i = 0; i < stringParts.size(); ++i) {
        auto idx = builder_.createConstInt(static_cast<int64_t>(i));
        auto strVal = builder_.createConstString(stringParts[i]);
        builder_.createSetElem(stringsArray, idx, strVal);
    }

    // Add 'raw' property to the strings array (same values for now)
    // TODO: Handle raw string escapes properly (e.g., `\n` vs actual newline)
    auto rawArray = builder_.createNewArrayBoxed(arrayLen, HIRType::makeString());
    for (size_t i = 0; i < stringParts.size(); ++i) {
        auto idx = builder_.createConstInt(static_cast<int64_t>(i));
        auto strVal = builder_.createConstString(stringParts[i]);
        builder_.createSetElem(rawArray, idx, strVal);
    }
    builder_.createSetPropStatic(stringsArray, "raw", rawArray);

    // Build argument list: [stringsArray, ...expressions]
    std::vector<std::shared_ptr<HIRValue>> args;
    args.push_back(stringsArray);
    for (const auto& expr : expressions) {
        args.push_back(expr);
    }

    // Call the tag function with indirect call (since tag could be any callable)
    lastValue_ = builder_.createCallIndirect(tagFn, args, HIRType::makeAny());
}

void ASTToHIR::visitAsExpression(ast::AsExpression* node) {
    // Type assertion - just lower the expression
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitNonNullExpression(ast::NonNullExpression* node) {
    // Non-null assertion - just lower the expression
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    auto operand = lowerExpression(node->operand.get());

    const std::string& op = node->op;
    if (op == "-") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }
        lastValue_ = isFloat ? builder_.createNegF64(operand) : builder_.createNegI64(operand);
    } else if (op == "!") {
        lastValue_ = builder_.createLogicalNot(operand);
    } else if (op == "~") {
        lastValue_ = builder_.createNotI64(operand);
    } else if (op == "+") {
        lastValue_ = operand;  // Unary plus is a no-op
    } else if (op == "typeof") {
        lastValue_ = builder_.createTypeOf(operand);
    } else if (op == "++" || op == "--") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }

        std::shared_ptr<HIRValue> result;
        if (isFloat) {
            auto one = builder_.createConstFloat(1.0);
            result = (op == "++") ? builder_.createAddF64(operand, one)
                                  : builder_.createSubF64(operand, one);
        } else {
            auto one = builder_.createConstInt(1);
            result = (op == "++") ? builder_.createAddI64(operand, one)
                                  : builder_.createSubI64(operand, one);
        }

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            // Check if this is a captured variable from an outer function
            size_t scopeIndex = 0;
            if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                // Store to captured variable
                auto* info = lookupVariableInfo(ident->name);
                auto type = info && info->elemType ? info->elemType : result->type;
                registerCapture(ident->name, type, scopeIndex);
                currentFunction_->hasClosure = true;
                builder_.createStoreCapture(ident->name, result);
            } else {
                auto* info = lookupVariableInfo(ident->name);
                if (info && info->isAlloca) {
                    builder_.createStore(result, info->value, info->elemType);
                } else {
                    defineVariable(ident->name, result);
                }
            }
        }
        // Handle property access (e.g., this.#count++ or obj.field++)
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get());
        if (prop) {
            auto obj = lowerExpression(prop->expression.get());
            std::string propName = prop->name;
            builder_.createSetPropStatic(obj, propName, result);
        }
        lastValue_ = result;  // Prefix returns new value
    } else {
        lastValue_ = operand;
    }
}

void ASTToHIR::visitDeleteExpression(ast::DeleteExpression* node) {
    // Handle delete obj.prop or delete obj["prop"]
    if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        // delete obj.prop
        auto obj = lowerExpression(propAccess->expression.get());
        auto key = builder_.createConstString(propAccess->name);
        lastValue_ = builder_.createCall("ts_object_delete_property", {obj, key}, HIRType::makeBool());
        return;
    }

    if (auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())) {
        // delete obj["prop"] or delete obj[key]
        auto obj = lowerExpression(elemAccess->expression.get());
        auto key = lowerExpression(elemAccess->argumentExpression.get());
        lastValue_ = builder_.createCall("ts_object_delete_property", {obj, key}, HIRType::makeBool());
        return;
    }

    // For other cases (like delete x), just return true
    // JavaScript spec says delete on non-references returns true
    lastValue_ = builder_.createConstBool(true);
}

void ASTToHIR::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    auto operand = lowerExpression(node->operand.get());
    auto oldValue = operand;

    const std::string& op = node->op;
    if (op == "++" || op == "--") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }

        std::shared_ptr<HIRValue> result;
        if (isFloat) {
            auto one = builder_.createConstFloat(1.0);
            result = (op == "++") ? builder_.createAddF64(operand, one)
                                  : builder_.createSubF64(operand, one);
        } else {
            auto one = builder_.createConstInt(1);
            result = (op == "++") ? builder_.createAddI64(operand, one)
                                  : builder_.createSubI64(operand, one);
        }

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            // Check if this is a captured variable from an outer function
            size_t scopeIndex = 0;
            if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                // Store to captured variable
                auto* info = lookupVariableInfo(ident->name);
                auto type = info && info->elemType ? info->elemType : result->type;
                registerCapture(ident->name, type, scopeIndex);
                currentFunction_->hasClosure = true;
                builder_.createStoreCapture(ident->name, result);
            } else {
                auto* info = lookupVariableInfo(ident->name);
                if (info && info->isAlloca) {
                    builder_.createStore(result, info->value, info->elemType);
                } else {
                    defineVariable(ident->name, result);
                }
            }
        }
        // Handle property access (e.g., this.#count++ or obj.field++)
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get());
        if (prop) {
            auto obj = lowerExpression(prop->expression.get());
            std::string propName = prop->name;
            builder_.createSetPropStatic(obj, propName, result);
        }
        // Postfix returns old value
        lastValue_ = oldValue;
    } else {
        lastValue_ = operand;
    }
}

void ASTToHIR::visitClassDeclaration(ast::ClassDeclaration* node) {
    // Create HIR class
    auto* hirClass = builder_.createClass(node->name);
    if (!hirClass) return;

    // Track the current class for 'this' handling
    HIRClass* savedClass = currentClass_;
    currentClass_ = hirClass;

    // Handle inheritance - look up base class
    if (!node->baseClass.empty()) {
        for (auto& cls : module_->classes) {
            if (cls->name == node->baseClass) {
                hirClass->baseClass = cls.get();
                break;
            }
        }
    }

    // Create class shape (layout of instance properties)
    auto shape = std::make_shared<HIRShape>();
    shape->className = node->name;

    // First pass: collect properties for the shape
    uint32_t propertyOffset = 0;

    // If we have a base class, copy its properties first
    if (hirClass->baseClass && hirClass->baseClass->shape) {
        auto baseShape = hirClass->baseClass->shape;
        shape->parent = baseShape.get();
        // Copy base class properties
        for (const auto& [name, offset] : baseShape->propertyOffsets) {
            shape->propertyOffsets[name] = offset;
        }
        for (const auto& [name, type] : baseShape->propertyTypes) {
            shape->propertyTypes[name] = type;
        }
        propertyOffset = baseShape->size;  // Start our properties after base class properties
    }

    // Add this class's own properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (!propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                shape->propertyOffsets[propDef->name] = propertyOffset;
                shape->propertyTypes[propDef->name] = propType;
                propertyOffset++;
            }
        }
    }
    shape->size = propertyOffset;
    hirClass->shape = shape;

    // Static property pass: create globals for static properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                std::string globalName = node->name + "_static_" + propDef->name;

                // Create global variable for the static property
                auto globalPtr = builder_.createGlobal(globalName, propType);
                staticPropertyGlobals_[globalName] = {globalPtr, propType};

                // Defer initialization to user_main
                if (propDef->initializer) {
                    deferredStaticInits_.push_back({globalPtr, propType, propDef->initializer.get()});
                }
            }
        }
        // Collect static blocks for deferred execution
        if (auto* staticBlock = dynamic_cast<ast::StaticBlock*>(memberPtr.get())) {
            deferredStaticBlocks_.push_back(staticBlock);
        }
    }

    // Second pass: create methods
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            // Skip abstract methods - they have no body
            if (methodDef->isAbstract || !methodDef->hasBody) {
                continue;
            }

            // Generate a unique function name for the method
            std::string methodFuncName;
            std::string methodKey = methodDef->name;  // Key used for registration in class
            if (methodDef->name == "constructor") {
                methodFuncName = node->name + "_constructor";
            } else if (methodDef->isGetter) {
                // Getter: ClassName___getter_propName
                methodFuncName = node->name + "___getter_" + methodDef->name;
                methodKey = "__getter_" + methodDef->name;
            } else if (methodDef->isSetter) {
                // Setter: ClassName___setter_propName
                methodFuncName = node->name + "___setter_" + methodDef->name;
                methodKey = "__setter_" + methodDef->name;
            } else if (methodDef->isStatic) {
                methodFuncName = node->name + "_static_" + methodDef->name;
            } else {
                methodFuncName = node->name + "_" + methodDef->name;
            }

            // Create HIR function for this method
            auto func = std::make_unique<HIRFunction>(methodFuncName);
            func->isAsync = methodDef->isAsync;
            func->isGenerator = methodDef->isGenerator;

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Add explicit parameters
            for (auto& param : methodDef->parameters) {
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }
                func->params.push_back({paramName, paramType});
            }

            // Set return type
            // Setters always return void, regardless of explicit type annotation
            if (methodDef->isSetter) {
                func->returnType = HIRType::makeVoid();
            } else {
                func->returnType = methodDef->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(methodDef->returnType);
            }

            // Save current function and create entry block
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = func.get();

            // Create entry block
            auto entryBlock = func->createBlock("entry");
            builder_.setInsertPoint(entryBlock);
            currentBlock_ = entryBlock;

            // Enter function scope
            pushFunctionScope(func.get());

            // Register parameters in scope
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
                defineVariable(paramName, paramValue);
            }
            func->nextValueId = static_cast<uint32_t>(func->params.size());

            // For constructors, initialize instance property default values before user code
            if (methodDef->name == "constructor") {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic && propDef->initializer) {
                                // Lower the initializer expression
                                auto initVal = lowerExpression(propDef->initializer.get());

                                // Store the initialized value to the property
                                builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                            }
                        }
                    }
                }
            }

            // Lower method body
            for (auto& stmt : methodDef->body) {
                lowerStatement(stmt.get());
            }

            // Add implicit return if no terminator
            if (!hasTerminator()) {
                builder_.createReturnVoid();
            }

            popScope();

            // Restore saved function
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register method in the class
            HIRFunction* funcPtr = func.get();
            if (methodDef->name == "constructor") {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                hirClass->staticMethods[methodDef->name] = funcPtr;
            } else {
                // Use methodKey for registration (includes __getter_/__setter_ prefix for accessors)
                hirClass->methods[methodKey] = funcPtr;
                // Add to vtable for virtual dispatch
                hirClass->vtable.push_back({methodKey, funcPtr});
            }

            // Add function to module
            module_->functions.push_back(std::move(func));
        }
    }

    // If no explicit constructor was defined, but we have property initializers,
    // generate a default constructor to initialize them
    if (!hirClass->constructor) {
        // Check if there are any property initializers
        bool hasPropertyInitializers = false;
        for (auto& memberPtr : node->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Also need constructor if we have a base class (to call super())
        bool needsDefaultConstructor = hasPropertyInitializers || hirClass->baseClass;

        if (needsDefaultConstructor) {
            std::string ctorName = node->name + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);

            // 'this' is the first parameter
            defaultCtor->params.push_back({"this", HIRType::makeObject()});
            defaultCtor->nextValueId = 1;

            // Create entry block
            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            // Define 'this' in scope
            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeObject(), "this");
            defineVariable("this", thisValue);

            // Call super() if we have a base class
            if (hirClass->baseClass && hirClass->baseClass->constructor) {
                std::vector<std::shared_ptr<HIRValue>> superArgs;
                superArgs.push_back(thisValue);
                builder_.createCall(hirClass->baseClass->constructor->name, superArgs, HIRType::makeVoid());
            }

            // Initialize property defaults
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic && propDef->initializer) {
                        auto initVal = lowerExpression(propDef->initializer.get());
                        builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                    }
                }
            }

            // Return void
            builder_.createReturnVoid();

            popScope();
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register the default constructor
            hirClass->constructor = defaultCtor.get();
            module_->functions.push_back(std::move(defaultCtor));
        }
    }

    // Restore class context
    currentClass_ = savedClass;
}

void ASTToHIR::visitClassExpression(ast::ClassExpression* node) {
    // Generate a unique class name for anonymous class expressions
    // Use the same naming convention as the analyzer (__anon_class_X)
    std::string className = node->name.empty()
        ? "__anon_class_" + std::to_string(classExprCounter_++)
        : node->name;

    // Create HIR class
    auto* hirClass = builder_.createClass(className);
    if (!hirClass) {
        lastValue_ = builder_.createConstNull();
        return;
    }

    // Track the current class for 'this' handling
    HIRClass* savedClass = currentClass_;
    currentClass_ = hirClass;

    // Handle inheritance - look up base class
    if (!node->baseClass.empty()) {
        for (auto& cls : module_->classes) {
            if (cls->name == node->baseClass) {
                hirClass->baseClass = cls.get();
                break;
            }
        }
    }

    // Create class shape (layout of instance properties)
    auto shape = std::make_shared<HIRShape>();
    shape->className = className;

    // First pass: collect properties for the shape
    uint32_t propertyOffset = 0;

    // If we have a base class, copy its properties first
    if (hirClass->baseClass && hirClass->baseClass->shape) {
        auto baseShape = hirClass->baseClass->shape;
        shape->parent = baseShape.get();
        // Copy base class properties
        for (const auto& [name, offset] : baseShape->propertyOffsets) {
            shape->propertyOffsets[name] = offset;
        }
        for (const auto& [name, type] : baseShape->propertyTypes) {
            shape->propertyTypes[name] = type;
        }
        propertyOffset = baseShape->size;  // Start our properties after base class properties
    }

    // Add this class's own properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (!propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                shape->propertyOffsets[propDef->name] = propertyOffset;
                shape->propertyTypes[propDef->name] = propType;
                propertyOffset++;
            }
        }
    }
    shape->size = propertyOffset;
    hirClass->shape = shape;

    // Static property pass: create globals for static properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                std::string globalName = className + "_static_" + propDef->name;

                // Create global variable for the static property
                auto globalPtr = builder_.createGlobal(globalName, propType);
                staticPropertyGlobals_[globalName] = {globalPtr, propType};

                // Defer initialization to user_main
                if (propDef->initializer) {
                    deferredStaticInits_.push_back({globalPtr, propType, propDef->initializer.get()});
                }
            }
        }
        // Collect static blocks for deferred execution
        if (auto* staticBlock = dynamic_cast<ast::StaticBlock*>(memberPtr.get())) {
            deferredStaticBlocks_.push_back(staticBlock);
        }
    }

    // Save the current insert point before processing methods
    // (so we can restore it after and not pollute method bodies with later instructions)
    HIRBlock* savedBlockBeforeMethods = currentBlock_;
    HIRFunction* savedFuncBeforeMethods = currentFunction_;

    // Second pass: create methods
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            // Skip abstract methods - they have no body
            if (methodDef->isAbstract || !methodDef->hasBody) {
                continue;
            }

            // Generate a unique function name for the method
            std::string methodFuncName;
            std::string methodKey = methodDef->name;  // Key used for registration in class
            if (methodDef->name == "constructor") {
                methodFuncName = className + "_constructor";
            } else if (methodDef->isGetter) {
                // Getter: ClassName___getter_propName
                methodFuncName = className + "___getter_" + methodDef->name;
                methodKey = "__getter_" + methodDef->name;
            } else if (methodDef->isSetter) {
                // Setter: ClassName___setter_propName
                methodFuncName = className + "___setter_" + methodDef->name;
                methodKey = "__setter_" + methodDef->name;
            } else if (methodDef->isStatic) {
                methodFuncName = className + "_static_" + methodDef->name;
            } else {
                methodFuncName = className + "_" + methodDef->name;
            }

            // Create HIR function for this method
            auto func = std::make_unique<HIRFunction>(methodFuncName);
            func->isAsync = methodDef->isAsync;
            func->isGenerator = methodDef->isGenerator;

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Add explicit parameters
            for (auto& param : methodDef->parameters) {
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }
                func->params.push_back({paramName, paramType});
            }

            // Set return type
            // Setters always return void, regardless of explicit type annotation
            if (methodDef->isSetter) {
                func->returnType = HIRType::makeVoid();
            } else {
                func->returnType = methodDef->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(methodDef->returnType);
            }

            // Save current function and create entry block
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = func.get();

            // Create entry block
            auto entryBlock = func->createBlock("entry");
            builder_.setInsertPoint(entryBlock);
            currentBlock_ = entryBlock;

            // Enter function scope
            pushFunctionScope(func.get());

            // Register parameters in scope
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
                defineVariable(paramName, paramValue);
            }
            func->nextValueId = static_cast<uint32_t>(func->params.size());

            // For constructors, initialize instance property default values before user code
            if (methodDef->name == "constructor") {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic && propDef->initializer) {
                                // Lower the initializer expression
                                auto initVal = lowerExpression(propDef->initializer.get());

                                // Store the initialized value to the property
                                builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                            }
                        }
                    }
                }
            }

            // Lower method body
            for (auto& stmt : methodDef->body) {
                lowerStatement(stmt.get());
            }

            // Add implicit return if no terminator
            if (!hasTerminator()) {
                builder_.createReturnVoid();
            }

            popScope();

            // Restore saved function
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register method in the class
            HIRFunction* funcPtr = func.get();
            if (methodDef->name == "constructor") {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                hirClass->staticMethods[methodDef->name] = funcPtr;
            } else {
                // Use methodKey for registration (includes __getter_/__setter_ prefix for accessors)
                hirClass->methods[methodKey] = funcPtr;
                // Add to vtable for virtual dispatch
                hirClass->vtable.push_back({methodKey, funcPtr});
            }

            // Add function to module
            module_->functions.push_back(std::move(func));
        }
    }

    // If no explicit constructor was defined, but we have property initializers,
    // generate a default constructor to initialize them
    if (!hirClass->constructor) {
        // Check if there are any property initializers
        bool hasPropertyInitializers = false;
        for (auto& memberPtr : node->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Also need constructor if we have a base class (to call super())
        bool needsDefaultConstructor = hasPropertyInitializers || hirClass->baseClass;

        if (needsDefaultConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);

            // 'this' is the first parameter
            defaultCtor->params.push_back({"this", HIRType::makeObject()});
            defaultCtor->nextValueId = 1;

            // Create entry block
            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            // Define 'this' in scope
            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeObject(), "this");
            defineVariable("this", thisValue);

            // Call super() if we have a base class
            if (hirClass->baseClass && hirClass->baseClass->constructor) {
                std::vector<std::shared_ptr<HIRValue>> superArgs;
                superArgs.push_back(thisValue);
                builder_.createCall(hirClass->baseClass->constructor->name, superArgs, HIRType::makeVoid());
            }

            // Initialize property defaults
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic && propDef->initializer) {
                        auto initVal = lowerExpression(propDef->initializer.get());
                        builder_.createSetPropStatic(thisValue, propDef->name, initVal);
                    }
                }
            }

            // Return void
            builder_.createReturnVoid();

            popScope();
            currentFunction_ = savedFunc;
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register the default constructor
            hirClass->constructor = defaultCtor.get();
            module_->functions.push_back(std::move(defaultCtor));
        }
    }

    // Restore the insert point to what it was before processing methods
    currentFunction_ = savedFuncBeforeMethods;
    currentBlock_ = savedBlockBeforeMethods;
    if (savedBlockBeforeMethods) {
        builder_.setInsertPoint(savedBlockBeforeMethods);
    }

    // Restore class context
    currentClass_ = savedClass;

    // Store the generated class name for variable tracking (used by visitVariableDeclaration)
    lastGeneratedClassName_ = className;

    // The result of a class expression is a reference to the class constructor
    // We use LoadFunction to get the constructor pointer
    if (hirClass->constructor) {
        lastValue_ = builder_.createLoadFunction(hirClass->constructor->name);
    } else {
        // If no explicit constructor, load the implicit default constructor
        // For now, just return a pointer to the class (the runtime will handle allocation)
        lastValue_ = builder_.createLoadFunction(className + "_constructor");
    }
}

void ASTToHIR::visitInterfaceDeclaration(ast::InterfaceDeclaration* node) {
    // Interfaces are type-only, nothing to generate
}

void ASTToHIR::visitObjectBindingPattern(ast::ObjectBindingPattern* node) {
    // Handled during variable declaration
}

void ASTToHIR::visitArrayBindingPattern(ast::ArrayBindingPattern* node) {
    // Handled during variable declaration
}

void ASTToHIR::visitBindingElement(ast::BindingElement* node) {
    // Handled during variable declaration
}

void ASTToHIR::visitSpreadElement(ast::SpreadElement* node) {
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitOmittedExpression(ast::OmittedExpression* node) {
    lastValue_ = builder_.createConstUndefined();
}

void ASTToHIR::visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) {
    // Type aliases are type-only, nothing to generate
}

void ASTToHIR::visitEnumDeclaration(ast::EnumDeclaration* node) {
    // Process enum members and store values
    std::map<std::string, EnumValue> members;
    std::map<int64_t, std::string> reverseMap;
    int64_t autoValue = 0;

    for (auto& member : node->members) {
        EnumValue ev;

        if (member.initializer) {
            // Has an explicit initializer
            if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(member.initializer.get())) {
                // Numeric initializer
                ev.isString = false;
                ev.numValue = static_cast<int64_t>(numLit->value);
                autoValue = ev.numValue + 1;  // Next auto value
                reverseMap[ev.numValue] = member.name;
            } else if (auto* strLit = dynamic_cast<ast::StringLiteral*>(member.initializer.get())) {
                // String initializer
                ev.isString = true;
                ev.strValue = strLit->value;
                // String enums don't have reverse mapping
            } else {
                // Other expression - treat as numeric for now (TODO: const eval)
                ev.isString = false;
                ev.numValue = autoValue++;
                reverseMap[ev.numValue] = member.name;
            }
        } else {
            // Auto-increment numeric value
            ev.isString = false;
            ev.numValue = autoValue++;
            reverseMap[ev.numValue] = member.name;
        }

        members[member.name] = ev;
    }

    enumValues_[node->name] = std::move(members);
    if (!reverseMap.empty()) {
        enumReverseMap_[node->name] = std::move(reverseMap);
    }
}

//==============================================================================
// JSX Lowering
//==============================================================================

// Helper to lower JSX attributes into a props object
std::shared_ptr<HIRValue> ASTToHIR::lowerJsxAttributes(const std::vector<ast::NodePtr>& attributes) {
    // Create a new object for props
    auto propsObj = builder_.createNewObjectDynamic();

    for (const auto& attr : attributes) {
        if (auto* jsxAttr = dynamic_cast<ast::JsxAttribute*>(attr.get())) {
            // Regular attribute: <div name={value} /> or <div name="string" />
            auto propName = builder_.createConstString(jsxAttr->name);
            std::shared_ptr<HIRValue> propValue;

            if (jsxAttr->initializer) {
                // Attribute has a value
                propValue = lowerExpression(jsxAttr->initializer.get());
            } else {
                // Boolean attribute: <div disabled /> means disabled={true}
                propValue = builder_.createConstBool(true);
            }

            builder_.createSetPropDynamic(propsObj, propName, propValue);
        } else if (auto* spreadAttr = dynamic_cast<ast::JsxSpreadAttribute*>(attr.get())) {
            // Spread attribute: <div {...props} />
            // For now, we'll just skip spread attributes (would need Object.assign)
            // A more complete implementation would merge the spread object into props
            if (spreadAttr->expression) {
                // TODO: Implement spread merging with Object.assign
                // For now, just evaluate the expression for side effects
                lowerExpression(spreadAttr->expression.get());
            }
        }
    }

    return propsObj;
}

// Helper to lower JSX children into an array
std::shared_ptr<HIRValue> ASTToHIR::lowerJsxChildren(const std::vector<ast::ExprPtr>& children) {
    // Create a new array for children
    auto childArray = builder_.createNewArrayBoxed(builder_.createConstInt(static_cast<int64_t>(children.size())));

    int64_t index = 0;
    for (const auto& child : children) {
        auto childValue = lowerExpression(child.get());
        auto indexVal = builder_.createConstInt(index++);
        builder_.createSetElem(childArray, indexVal, childValue);
    }

    return childArray;
}

void ASTToHIR::visitJsxElement(ast::JsxElement* node) {
    // Lower JSX element: <tagName attributes>children</tagName>
    // Creates an object { type: tagName, props: {...}, children: [...] }

    // Create tag name string
    auto tagName = builder_.createConstString(node->tagName);

    // Lower attributes to props object
    auto props = lowerJsxAttributes(node->attributes);

    // Lower children to array
    auto children = lowerJsxChildren(node->children);

    // Call ts_jsx_create_element(tagName, props, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) {
    // Lower self-closing JSX element: <tagName attributes />
    // Same as JsxElement but with empty children

    // Create tag name string
    auto tagName = builder_.createConstString(node->tagName);

    // Lower attributes to props object
    auto props = lowerJsxAttributes(node->attributes);

    // Create empty children array
    auto children = builder_.createNewArrayBoxed(builder_.createConstInt(0));

    // Call ts_jsx_create_element(tagName, props, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxFragment(ast::JsxFragment* node) {
    // Lower JSX fragment: <>children</>
    // Fragments have null tagName and no props

    // Null tagName for fragments
    auto tagName = builder_.createConstNull();

    // Empty props object for fragments
    auto props = builder_.createNewObjectDynamic();

    // Lower children to array
    auto children = lowerJsxChildren(node->children);

    // Call ts_jsx_create_element(null, {}, children)
    lastValue_ = builder_.createCall("ts_jsx_create_element", {tagName, props, children}, HIRType::makeObject());
}

void ASTToHIR::visitJsxExpression(ast::JsxExpression* node) {
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitJsxText(ast::JsxText* node) {
    lastValue_ = builder_.createConstString(node->text);
}

} // namespace ts::hir
