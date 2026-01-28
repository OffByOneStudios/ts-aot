#pragma once

#include "../ast/AstNodes.h"
#include "HIR.h"
#include "HIRBuilder.h"
#include "../analysis/Type.h"

#include <map>
#include <stack>
#include <string>

namespace ts::hir {

//==============================================================================
// ASTToHIR - Lowers AST to High-Level IR
//
// This pass:
// 1. Converts AST statements to HIR blocks with proper control flow
// 2. Converts expressions to SSA values
// 3. Generates HIR types from the analyzer's Type system
// 4. Creates GC-aware allocation operations (stubs for Boehm initially)
//==============================================================================

class ASTToHIR : public ast::Visitor {
public:
    ASTToHIR();
    ~ASTToHIR() override = default;

    // Main entry point - lower a program to HIR module
    std::unique_ptr<HIRModule> lower(ast::Program* program, const std::string& moduleName);

private:
    //==========================================================================
    // State
    //==========================================================================

    std::unique_ptr<HIRModule> module_;
    HIRFunction* currentFunction_ = nullptr;
    HIRBlock* currentBlock_ = nullptr;
    HIRBuilder builder_;

    // Value counter for SSA names (%v0, %v1, etc.)
    int valueCounter_ = 0;

    // Variable name to SSA value mapping (scoped)
    // Variables can be either:
    // - Direct values (function parameters): isAlloca=false, value is the actual value
    // - Stack variables (let/var/const): isAlloca=true, value is the alloca pointer, elemType is the stored type
    struct VariableInfo {
        std::shared_ptr<HIRValue> value;
        std::shared_ptr<HIRType> elemType;  // For allocas: the stored type; null for direct values
        bool isAlloca = false;
    };
    struct Scope {
        std::map<std::string, VariableInfo> variables;
        bool isFunctionBoundary = false;  // True if this scope starts a new function
        HIRFunction* owningFunction = nullptr;  // The function this scope belongs to
    };
    std::vector<Scope> scopes_;

    // Track captured variables for the current nested function being lowered
    // Maps variable name to (outer scope index, type) for variables that need capturing
    struct CaptureInfo {
        std::string name;
        std::shared_ptr<HIRType> type;
        size_t outerScopeIndex;  // Which scope the variable was found in
    };
    std::vector<CaptureInfo> pendingCaptures_;

    // The scope index where the current nested function begins (for capture detection)
    size_t currentFunctionScopeStart_ = 0;

    // Control flow targets for break/continue
    struct LoopContext {
        HIRBlock* continueTarget;
        HIRBlock* breakTarget;
    };
    std::stack<LoopContext> loopStack_;

    // For switch statements
    struct SwitchContext {
        HIRBlock* breakTarget;
        std::vector<std::pair<int64_t, HIRBlock*>> cases;
        HIRBlock* defaultCase;
    };
    std::stack<SwitchContext> switchStack_;

    //==========================================================================
    // Visitor Implementation - Statements
    //==========================================================================

    void visitProgram(ast::Program* node) override;
    void visitFunctionDeclaration(ast::FunctionDeclaration* node) override;
    void visitVariableDeclaration(ast::VariableDeclaration* node) override;
    void visitExpressionStatement(ast::ExpressionStatement* node) override;
    void visitBlockStatement(ast::BlockStatement* node) override;
    void visitReturnStatement(ast::ReturnStatement* node) override;
    void visitIfStatement(ast::IfStatement* node) override;
    void visitWhileStatement(ast::WhileStatement* node) override;
    void visitForStatement(ast::ForStatement* node) override;
    void visitForOfStatement(ast::ForOfStatement* node) override;
    void visitForInStatement(ast::ForInStatement* node) override;
    void visitBreakStatement(ast::BreakStatement* node) override;
    void visitContinueStatement(ast::ContinueStatement* node) override;
    void visitSwitchStatement(ast::SwitchStatement* node) override;
    void visitTryStatement(ast::TryStatement* node) override;
    void visitThrowStatement(ast::ThrowStatement* node) override;
    void visitImportDeclaration(ast::ImportDeclaration* node) override;
    void visitExportDeclaration(ast::ExportDeclaration* node) override;
    void visitExportAssignment(ast::ExportAssignment* node) override;

    //==========================================================================
    // Visitor Implementation - Expressions
    //==========================================================================

    void visitBinaryExpression(ast::BinaryExpression* node) override;
    void visitConditionalExpression(ast::ConditionalExpression* node) override;
    void visitAssignmentExpression(ast::AssignmentExpression* node) override;
    void visitCallExpression(ast::CallExpression* node) override;
    void visitNewExpression(ast::NewExpression* node) override;
    void visitParenthesizedExpression(ast::ParenthesizedExpression* node) override;
    void visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) override;
    void visitElementAccessExpression(ast::ElementAccessExpression* node) override;
    void visitPropertyAccessExpression(ast::PropertyAccessExpression* node) override;
    void visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) override;
    void visitPropertyAssignment(ast::PropertyAssignment* node) override;
    void visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) override;
    void visitComputedPropertyName(ast::ComputedPropertyName* node) override;
    void visitMethodDefinition(ast::MethodDefinition* node) override;
    void visitStaticBlock(ast::StaticBlock* node) override;
    void visitIdentifier(ast::Identifier* node) override;
    void visitSuperExpression(ast::SuperExpression* node) override;
    void visitStringLiteral(ast::StringLiteral* node) override;
    void visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) override;
    void visitNumericLiteral(ast::NumericLiteral* node) override;
    void visitBigIntLiteral(ast::BigIntLiteral* node) override;
    void visitBooleanLiteral(ast::BooleanLiteral* node) override;
    void visitNullLiteral(ast::NullLiteral* node) override;
    void visitUndefinedLiteral(ast::UndefinedLiteral* node) override;
    void visitAwaitExpression(ast::AwaitExpression* node) override;
    void visitYieldExpression(ast::YieldExpression* node) override;
    void visitDynamicImport(ast::DynamicImport* node) override;
    void visitArrowFunction(ast::ArrowFunction* node) override;
    void visitFunctionExpression(ast::FunctionExpression* node) override;
    void visitTemplateExpression(ast::TemplateExpression* node) override;
    void visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) override;
    void visitAsExpression(ast::AsExpression* node) override;
    void visitNonNullExpression(ast::NonNullExpression* node) override;
    void visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) override;
    void visitDeleteExpression(ast::DeleteExpression* node) override;
    void visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) override;
    void visitClassDeclaration(ast::ClassDeclaration* node) override;
    void visitClassExpression(ast::ClassExpression* node) override;
    void visitInterfaceDeclaration(ast::InterfaceDeclaration* node) override;
    void visitObjectBindingPattern(ast::ObjectBindingPattern* node) override;
    void visitArrayBindingPattern(ast::ArrayBindingPattern* node) override;
    void visitBindingElement(ast::BindingElement* node) override;
    void visitSpreadElement(ast::SpreadElement* node) override;
    void visitOmittedExpression(ast::OmittedExpression* node) override;
    void visitTypeAliasDeclaration(ast::TypeAliasDeclaration* node) override;
    void visitEnumDeclaration(ast::EnumDeclaration* node) override;

    // JSX
    void visitJsxElement(ast::JsxElement* node) override;
    void visitJsxSelfClosingElement(ast::JsxSelfClosingElement* node) override;
    void visitJsxFragment(ast::JsxFragment* node) override;
    void visitJsxExpression(ast::JsxExpression* node) override;
    void visitJsxText(ast::JsxText* node) override;

    //==========================================================================
    // Expression Lowering Helpers
    //==========================================================================

    // Lower an expression and return its SSA value
    std::shared_ptr<HIRValue> lowerExpression(ast::Expression* expr);

    // Lower a statement
    void lowerStatement(ast::Statement* stmt);

    //==========================================================================
    // Type Conversion
    //==========================================================================

    // Convert analyzer Type to HIR Type
    std::shared_ptr<HIRType> convertType(const std::shared_ptr<ts::Type>& type);

    // Convert a TypeScript type string (e.g. "number", "string") to HIR Type
    std::shared_ptr<HIRType> convertTypeFromString(const std::string& typeStr);

    //==========================================================================
    // SSA Helpers
    //==========================================================================

    // Create a new SSA value with auto-incremented name
    std::shared_ptr<HIRValue> createValue(std::shared_ptr<HIRType> type);

    // Create a new basic block with auto-generated label
    HIRBlock* createBlock(const std::string& hint = "bb");
    int blockCounter_ = 0;

    // Counter for generating unique arrow function names
    int arrowFuncCounter_ = 0;

    // Counter for generating unique function expression names
    int funcExprCounter_ = 0;

    // Scope management
    void pushScope();
    void pushFunctionScope(HIRFunction* func);  // Push scope that marks function boundary
    void popScope();
    void defineVariable(const std::string& name, std::shared_ptr<HIRValue> value);
    void defineVariableAlloca(const std::string& name, std::shared_ptr<HIRValue> allocaPtr,
                               std::shared_ptr<HIRType> elemType);
    VariableInfo* lookupVariableInfo(const std::string& name);
    std::shared_ptr<HIRValue> lookupVariable(const std::string& name);

    // Closure capture helpers
    // Looks up a variable and determines if it's captured from an outer function
    // Returns true if the variable crosses a function boundary
    bool isCapturedVariable(const std::string& name, size_t* outScopeIndex = nullptr);

    // Register a captured variable for the current function being lowered
    void registerCapture(const std::string& name, std::shared_ptr<HIRType> type, size_t scopeIndex);

    // Get all captures for the current nested function
    const std::vector<CaptureInfo>& getPendingCaptures() const { return pendingCaptures_; }
    void clearPendingCaptures() { pendingCaptures_.clear(); }

    //==========================================================================
    // Control Flow Helpers
    //==========================================================================

    // Emit a branch if the current block doesn't have a terminator
    void emitBranchIfNeeded(HIRBlock* target);

    // Check if current block has a terminator
    bool hasTerminator() const;

    //==========================================================================
    // Result Storage
    //==========================================================================

    // The result of the last expression lowered
    std::shared_ptr<HIRValue> lastValue_;
};

} // namespace ts::hir
