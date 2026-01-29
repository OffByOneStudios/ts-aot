#include "ASTToHIR.h"
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

    // Visit all statements in the program
    for (auto& stmt : program->body) {
        lowerStatement(stmt.get());
    }

    popScope();
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

    if (info->isAlloca && info->elemType) {
        // Emit a load for alloca-stored variables
        return builder_.createLoad(info->elemType, info->value);
    }
    return info->value;
}

bool ASTToHIR::isCapturedVariable(const std::string& name, size_t* outScopeIndex) {
    // Search from innermost to outermost scope
    // Track if we cross a function boundary (excluding the innermost function's own boundary)
    bool crossedFunctionBoundary = false;
    size_t scopeIndex = scopes_.size();

    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it, --scopeIndex) {
        // Check if we're crossing into a different function's scope BEFORE looking up
        // Skip the first function boundary (which is the current function)
        if (it->isFunctionBoundary && it != scopes_.rbegin()) {
            crossedFunctionBoundary = true;
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
        case ts::TypeKind::Class:
            return HIRType::makeObject();
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
    for (auto& init : deferredStaticInits_) {
        auto initVal = lowerExpression(init.initExpr);
        builder_.createStore(initVal, init.globalPtr, init.propType);
    }
    deferredStaticInits_.clear();  // Only emit once
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
    for (auto& param : node->parameters) {
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

    // Register parameters in the scope so they can be looked up
    // Parameter values have IDs 0, 1, 2, ... matching their index in HIRToLLVM
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [paramName, paramType] = func->params[i];
        // Create a value representing this parameter with specific ID
        auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);
        defineVariable(paramName, paramValue);
    }
    // Update function's value counter to start after parameters
    // This ensures new values created in the function body don't reuse parameter IDs
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // If this is user_main, emit deferred static property initializations
    if (node->name == "user_main") {
        emitDeferredStaticInits();
    }

    // Lower function body
    for (auto& stmt : node->body) {
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
    } else {
        initValue = builder_.createConstUndefined();
    }

    // Handle the binding pattern
    if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
        // Simple identifier - create variable directly
        std::shared_ptr<HIRType> varType = HIRType::makeAny();
        if (node->initializer && node->initializer->inferredType) {
            varType = convertType(node->initializer->inferredType);
        } else if (!node->type.empty()) {
            varType = convertTypeFromString(node->type);
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
    loopStack_.push({condBlock, endBlock});

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

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForStatement(ast::ForStatement* node) {
    auto* condBlock = createBlock("for.cond");
    auto* bodyBlock = createBlock("for.body");
    auto* updateBlock = createBlock("for.update");
    auto* endBlock = createBlock("for.end");

    // Push loop context
    loopStack_.push({updateBlock, endBlock});

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
    popScope();

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForOfStatement(ast::ForOfStatement* node) {
    // Simplified for-of: get iterator and loop
    auto* condBlock = createBlock("forof.cond");
    auto* bodyBlock = createBlock("forof.body");
    auto* endBlock = createBlock("forof.end");

    loopStack_.push({condBlock, endBlock});
    pushScope();

    // Get the iterable
    auto* iterExpr = dynamic_cast<ast::Expression*>(node->expression.get());
    auto iterable = iterExpr ? lowerExpression(iterExpr) : createValue(HIRType::makeAny());

    // Create iterator (simplified - just use array length for now)
    auto indexVal = createValue(HIRType::makeInt64());
    builder_.createConstInt(indexVal, 0);

    auto lenVal = builder_.createArrayLength(iterable);

    builder_.createBranch(condBlock);

    // Condition: index < length
    builder_.setInsertPoint(condBlock);
    currentBlock_ = condBlock;
    auto cond = builder_.createCmpLtI64(indexVal, lenVal);
    builder_.createCondBranch(cond, bodyBlock, endBlock);

    // Body: get element and execute body
    builder_.setInsertPoint(bodyBlock);
    currentBlock_ = bodyBlock;

    // Get current element
    auto elemVal = builder_.createGetElem(iterable, indexVal);

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

    // Increment index
    auto one = builder_.createConstInt(1);
    auto newIndex = builder_.createAddI64(indexVal, one);
    // Note: In real SSA, we'd need a phi node here. For now, this is simplified.

    emitBranchIfNeeded(condBlock);

    loopStack_.pop();
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForInStatement(ast::ForInStatement* node) {
    // Simplified for-in: iterate over keys
    auto* bodyBlock = createBlock("forin.body");
    auto* endBlock = createBlock("forin.end");

    loopStack_.push({bodyBlock, endBlock});
    pushScope();

    // Get the object to iterate
    auto* objExpr = dynamic_cast<ast::Expression*>(node->expression.get());
    auto obj = objExpr ? lowerExpression(objExpr) : createValue(HIRType::makeObject());

    // For now, just branch to end (proper implementation needs Object.keys)
    builder_.createBranch(endBlock);

    loopStack_.pop();
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitBreakStatement(ast::BreakStatement* node) {
    if (!loopStack_.empty()) {
        builder_.createBranch(loopStack_.top().breakTarget);
    }
}

void ASTToHIR::visitContinueStatement(ast::ContinueStatement* node) {
    if (!loopStack_.empty()) {
        builder_.createBranch(loopStack_.top().continueTarget);
    }
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

    // Determine if we should use Float64 operations (if either operand is Float64)
    bool useFloat = isFloat64(lhs, node->left.get()) || isFloat64(rhs, node->right.get());

    if (op == "+") {
        // Check if either operand is a string - if so, use string concatenation
        if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
            lastValue_ = builder_.createStringConcat(lhs, rhs);
        } else if (useFloat) {
            lastValue_ = builder_.createAddF64(lhs, rhs);
        } else {
            lastValue_ = builder_.createAddI64(lhs, rhs);
        }
    } else if (op == "-") {
        lastValue_ = useFloat ? builder_.createSubF64(lhs, rhs) : builder_.createSubI64(lhs, rhs);
    } else if (op == "*") {
        lastValue_ = useFloat ? builder_.createMulF64(lhs, rhs) : builder_.createMulI64(lhs, rhs);
    } else if (op == "/") {
        lastValue_ = useFloat ? builder_.createDivF64(lhs, rhs) : builder_.createDivI64(lhs, rhs);
    } else if (op == "%") {
        lastValue_ = useFloat ? builder_.createModF64(lhs, rhs) : builder_.createModI64(lhs, rhs);
    } else if (op == "<") {
        lastValue_ = useFloat ? builder_.createCmpLtF64(lhs, rhs) : builder_.createCmpLtI64(lhs, rhs);
    } else if (op == "<=") {
        lastValue_ = useFloat ? builder_.createCmpLeF64(lhs, rhs) : builder_.createCmpLeI64(lhs, rhs);
    } else if (op == ">") {
        lastValue_ = useFloat ? builder_.createCmpGtF64(lhs, rhs) : builder_.createCmpGtI64(lhs, rhs);
    } else if (op == ">=") {
        lastValue_ = useFloat ? builder_.createCmpGeF64(lhs, rhs) : builder_.createCmpGeI64(lhs, rhs);
    } else if (op == "==" || op == "===") {
        lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
    } else if (op == "!=" || op == "!==") {
        lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
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
    if (superExpr && currentClass_ && currentClass_->baseClass && currentClass_->baseClass->constructor) {
        // Call parent constructor with [this, ...args]
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
        // Not a local variable - direct function call
        lastValue_ = builder_.createCall(ident->name, args, HIRType::makeAny());
        return;
    }

    // Generic case
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitNewExpression(ast::NewExpression* node) {
    // Lower constructor arguments
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Get constructor/class name
    auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
    std::string className = ident ? ident->name : "Object";

    // Create new object
    auto newObj = builder_.createNewObjectDynamic();

    // Look up the class and call its constructor if it exists
    HIRClass* hirClass = nullptr;
    for (auto& cls : module_->classes) {
        if (cls->name == className) {
            hirClass = cls.get();
            break;
        }
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
                auto spreadArr = lowerExpression(spread->expression.get());
                builder_.createCall("ts_array_concat", {arr, spreadArr}, HIRType::makeVoid());
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
        val = createValue(HIRType::makeAny());
        builder_.createConstUndefined(val);
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

    // Check for known global objects
    static const std::set<std::string> knownGlobals = {
        "console", "Math", "JSON", "Object", "Array", "String", "Number",
        "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
        "process", "global", "globalThis", "undefined", "NaN", "Infinity"
    };

    if (knownGlobals.count(node->name)) {
        // Handle special constants
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

        // Emit LoadGlobal for global objects
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
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
    // TODO: RegExp support
    lastValue_ = builder_.createConstString(node->text);
}

void ASTToHIR::visitNumericLiteral(ast::NumericLiteral* node) {
    // In TypeScript/JavaScript, all numbers are IEEE 754 double-precision floats
    lastValue_ = builder_.createConstFloat(node->value);
}

void ASTToHIR::visitBigIntLiteral(ast::BigIntLiteral* node) {
    // TODO: BigInt support - for now convert to int
    lastValue_ = builder_.createConstInt(0);
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
        }
    } else if (auto* exprBody = dynamic_cast<ast::Expression*>(node->body.get())) {
        // Expression body - implicit return
        auto retVal = lowerExpression(exprBody);
        builder_.createReturn(retVal);
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
    } else {
        // Plain function pointer
        lastValue_ = builder_.createLoadFunction(funcName);
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
        // Plain function pointer
        lastValue_ = builder_.createLoadFunction(funcName);
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
        return builder_.createLoadFunction(funcName);
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
        lastValue_ = builder_.createNegI64(operand);
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
            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                builder_.createStore(result, info->value, info->elemType);
            } else {
                defineVariable(ident->name, result);
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
    // TODO: Proper delete support
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
            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                builder_.createStore(result, info->value, info->elemType);
            } else {
                defineVariable(ident->name, result);
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
    }

    // Second pass: create methods
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
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

    // Restore class context
    currentClass_ = savedClass;
}

void ASTToHIR::visitClassExpression(ast::ClassExpression* node) {
    // TODO: Proper class expression lowering
    lastValue_ = createValue(HIRType::makePtr());
    builder_.createConstNull(lastValue_);
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
    // TODO: Enum support
}

//==============================================================================
// JSX Lowering (stubs)
//==============================================================================

void ASTToHIR::visitJsxElement(ast::JsxElement* node) {
    // TODO: JSX support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) {
    // TODO: JSX support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitJsxFragment(ast::JsxFragment* node) {
    // TODO: JSX support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstNull(lastValue_);
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
