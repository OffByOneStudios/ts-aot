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
    scopes_.push_back(Scope{});
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

//==============================================================================
// Control Flow Helpers
//==============================================================================

void ASTToHIR::emitBranchIfNeeded(HIRBlock* target) {
    if (!hasTerminator()) {
        builder_.createBranch(target);
    }
}

bool ASTToHIR::hasTerminator() const {
    if (!currentBlock_ || currentBlock_->instructions.empty()) {
        return false;
    }
    auto& last = currentBlock_->instructions.back();
    // Check if last instruction is a terminator
    auto op = last->opcode;
    return op == HIROpcode::Branch || op == HIROpcode::CondBranch ||
           op == HIROpcode::Return || op == HIROpcode::ReturnVoid;
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
        // In TypeScript, 'number' can be int or float. For now, treat as int64
        // when it appears in function return types (most common case)
        return HIRType::makeInt64();
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
        case ts::TypeKind::Double:
            // TypeScript 'number' (Int or Double) is unified to Int64 for now
            // This simplifies the HIR prototype - all number operations are i64
            return HIRType::makeInt64();
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
        case ts::TypeKind::Function:
            return HIRType::makePtr();  // Function pointers
        default:
            return HIRType::makeAny();
    }
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

    // Enter function scope
    pushScope();

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

    std::string varName;
    if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
        varName = ident->name;
    } else {
        // Binding pattern - for now, use a placeholder name
        varName = "_binding" + std::to_string(blockCounter_);
    }

    // Get variable type from initializer or inferred type
    std::shared_ptr<HIRType> varType = HIRType::makeAny();
    if (node->initializer && node->initializer->inferredType) {
        varType = convertType(node->initializer->inferredType);
    } else if (!node->type.empty()) {
        varType = convertTypeFromString(node->type);
    }

    // Create alloca for the variable (stack slot)
    auto allocaPtr = builder_.createAlloca(varType, varName);

    // Lower and store initial value
    std::shared_ptr<HIRValue> initValue;
    if (node->initializer) {
        initValue = lowerExpression(node->initializer.get());
    } else {
        // Default to undefined
        initValue = builder_.createConstUndefined();
    }

    builder_.createStore(initValue, allocaPtr);

    // Register as alloca-based variable
    defineVariableAlloca(varName, allocaPtr, varType);
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
    // Simplified try: just lower the try block
    // TODO: Proper exception handling
    // tryBlock is a vector<StmtPtr>
    for (auto& stmt : node->tryBlock) {
        lowerStatement(stmt.get());
    }
}

void ASTToHIR::visitThrowStatement(ast::ThrowStatement* node) {
    // Lower the throw expression
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
    // TODO: Proper throw handling
    builder_.createUnreachable();
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
    auto lhs = lowerExpression(node->left.get());
    auto rhs = lowerExpression(node->right.get());

    const std::string& op = node->op;

    if (op == "+") {
        lastValue_ = builder_.createAddI64(lhs, rhs);
    } else if (op == "-") {
        lastValue_ = builder_.createSubI64(lhs, rhs);
    } else if (op == "*") {
        lastValue_ = builder_.createMulI64(lhs, rhs);
    } else if (op == "/") {
        lastValue_ = builder_.createDivI64(lhs, rhs);
    } else if (op == "%") {
        lastValue_ = builder_.createModI64(lhs, rhs);
    } else if (op == "<") {
        lastValue_ = builder_.createCmpLtI64(lhs, rhs);
    } else if (op == "<=") {
        lastValue_ = builder_.createCmpLeI64(lhs, rhs);
    } else if (op == ">") {
        lastValue_ = builder_.createCmpGtI64(lhs, rhs);
    } else if (op == ">=") {
        lastValue_ = builder_.createCmpGeI64(lhs, rhs);
    } else if (op == "==" || op == "===") {
        lastValue_ = builder_.createCmpEqI64(lhs, rhs);
    } else if (op == "!=" || op == "!==") {
        lastValue_ = builder_.createCmpNeI64(lhs, rhs);
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
        // Look up variable info to see if it's an alloca
        auto* info = lookupVariableInfo(ident->name);
        if (info && info->isAlloca) {
            // Emit store to the alloca
            builder_.createStore(rhs, info->value);
        } else if (info) {
            // Direct value - promote to alloca for mutability
            // Create new alloca and store
            auto allocaPtr = builder_.createAlloca(rhs->type, ident->name);
            builder_.createStore(rhs, allocaPtr);
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
        auto obj = lowerExpression(propAccess->expression.get());
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

    // Handle method call
    auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get());
    if (propAccess) {
        auto obj = lowerExpression(propAccess->expression.get());
        lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, HIRType::makeAny());
        return;
    }

    // Handle direct function call
    auto* ident = dynamic_cast<ast::Identifier*>(node->callee.get());
    if (ident) {
        lastValue_ = builder_.createCall(ident->name, args, HIRType::makeAny());
        return;
    }

    // Generic case
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitNewExpression(ast::NewExpression* node) {
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Get constructor name
    auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
    std::string className = ident ? ident->name : "Object";

    // Create new object
    lastValue_ = builder_.createNewObjectDynamic();
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

void ASTToHIR::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    auto obj = lowerExpression(node->expression.get());
    auto idx = lowerExpression(node->argumentExpression.get());
    lastValue_ = builder_.createGetElem(obj, idx);
}

void ASTToHIR::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    auto obj = lowerExpression(node->expression.get());
    lastValue_ = builder_.createGetPropStatic(obj, node->name, HIRType::makeAny());
}

void ASTToHIR::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    auto obj = builder_.createNewObjectDynamic();

    for (auto& prop : node->properties) {
        // Save the object before visiting property (which may overwrite lastValue_)
        lastValue_ = obj;
        prop->accept(this);
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
    // First check for local/parameter variables
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
    // Check if it's an integer
    if (node->value == static_cast<int64_t>(node->value)) {
        lastValue_ = builder_.createConstInt(static_cast<int64_t>(node->value));
    } else {
        lastValue_ = builder_.createConstFloat(node->value);
    }
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
    // TODO: Proper async/await
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitYieldExpression(ast::YieldExpression* node) {
    // TODO: Proper generator support
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitDynamicImport(ast::DynamicImport* node) {
    // TODO: Dynamic import support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}

void ASTToHIR::visitArrowFunction(ast::ArrowFunction* node) {
    // TODO: Proper arrow function lowering
    lastValue_ = createValue(HIRType::makePtr());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitFunctionExpression(ast::FunctionExpression* node) {
    // TODO: Proper function expression lowering
    lastValue_ = createValue(HIRType::makePtr());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitTemplateExpression(ast::TemplateExpression* node) {
    // Concatenate template parts
    lastValue_ = builder_.createConstString("");

    for (auto& span : node->spans) {
        // TODO: Concatenate spans
    }
}

void ASTToHIR::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    // TODO: Tagged template support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
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
        auto one = builder_.createConstInt(1);
        auto result = (op == "++") ? builder_.createAddI64(operand, one)
                                    : builder_.createSubI64(operand, one);

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                builder_.createStore(result, info->value);
            } else {
                defineVariable(ident->name, result);
            }
        }
        lastValue_ = result;
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
        auto one = builder_.createConstInt(1);
        auto result = (op == "++") ? builder_.createAddI64(operand, one)
                                    : builder_.createSubI64(operand, one);

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                builder_.createStore(result, info->value);
            } else {
                defineVariable(ident->name, result);
            }
        }
        // Postfix returns old value
        lastValue_ = oldValue;
    } else {
        lastValue_ = operand;
    }
}

void ASTToHIR::visitClassDeclaration(ast::ClassDeclaration* node) {
    // TODO: Proper class lowering
    // For now, create a class in the module
    auto* hirClass = builder_.createClass(node->name);
    if (!hirClass) return;

    // Add fields and methods
    for (auto& member : node->members) {
        // TODO: Process class members
    }
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
