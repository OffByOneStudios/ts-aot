#pragma once

#include "../ast/AstNodes.h"
#include "HIR.h"
#include "HIRBuilder.h"
#include "../analysis/Type.h"
#include "../analysis/Module.h"
#include "../analysis/Monomorphizer.h"

#include <map>
#include <set>
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

    // Main entry point - lower a program to HIR module (legacy interface)
    std::unique_ptr<HIRModule> lower(ast::Program* program, const std::string& moduleName);

    // New interface with specializations from Monomorphizer
    std::unique_ptr<HIRModule> lower(ast::Program* program,
                                     const std::vector<Specialization>& specializations,
                                     const std::string& moduleName);

private:
    //==========================================================================
    // State
    //==========================================================================

    std::unique_ptr<HIRModule> module_;
    HIRFunction* currentFunction_ = nullptr;
    HIRBlock* currentBlock_ = nullptr;
    HIRBuilder builder_;

    // Store specializations for looking up function info during call generation
    const std::vector<Specialization>* specializations_ = nullptr;

    // Current module path for cross-module function name disambiguation
    std::string currentModulePath_;

    // Helper: generate module-prefixed global variable name
    // Returns "__modvar_<name>" for the main file, "__modvar_<name>_m<hash>" for imported modules
    std::string modVarName(const std::string& name) const {
        if (currentModulePath_.empty()) {
            return "__modvar_" + name;
        }
        std::hash<std::string> hasher;
        auto hash = hasher(currentModulePath_) % 999999;
        return "__modvar_" + name + "_m" + std::to_string(hash);
    }

    // Map from locally imported name -> (extension module name, exported name)
    // E.g., { "join" -> {"path", "join"} } for `import { join } from 'path'`
    std::map<std::string, std::pair<std::string, std::string>> extensionImports_;

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
        // Closure capture tracking: when a variable is captured by a nested function,
        // we need to also access it via the closure's cell in the outer function.
        bool isCapturedByNested = false;
        std::shared_ptr<HIRValue> closurePtr = nullptr;  // The closure that owns the cell
        int captureIndex = -1;  // Index of this variable in the closure's captures array
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

    // Labeled loop targets for labeled break/continue
    std::map<std::string, LoopContext> labeledLoops_;

    // Pending label for the next loop (set by visitLabeledStatement)
    std::string pendingLabel_;

    // For switch statements
    struct SwitchContext {
        HIRBlock* breakTarget;
        std::vector<std::pair<int64_t, HIRBlock*>> cases;
        HIRBlock* defaultCase;
    };
    std::stack<SwitchContext> switchStack_;

    // Unified break target stack - both loops and switches push here.
    // break uses the top of this stack; continue uses loopStack_.
    std::stack<HIRBlock*> breakTargetStack_;

    // Class context - tracks when we're inside a class body
    HIRClass* currentClass_ = nullptr;

    // Class expression tracking - maps variable names to class names for class expressions
    // E.g., "const MyClass = class { ... }" maps "MyClass" -> "__class_expr_0" or generated name
    std::map<std::string, std::string> variableToClassName_;

    // Last generated class name from visitClassExpression (used by visitVariableDeclaration)
    std::string lastGeneratedClassName_;

    // Static property globals - maps "ClassName_static_propName" to (globalPtr, type)
    std::map<std::string, std::pair<std::shared_ptr<HIRValue>, std::shared_ptr<HIRType>>> staticPropertyGlobals_;

    // Module-scoped variable declarations from imported modules
    // Maps variable name to the AST VariableDeclaration node (for lazy initialization)
    std::map<std::string, ast::VariableDeclaration*> moduleVarDecls_;
    // Set of module-scoped variable names that have been promoted to globals
    std::set<std::string> moduleGlobalVars_;
    // Module globals accessed by inner (nested) functions -- the defining function
    // must read/write these from __modvar_ globals, not local allocas
    std::set<std::string> moduleGlobalsUsedByInner_;
    // Source file of the main program (to distinguish imported modules)
    std::string mainSourceFile_;

    // Enum values - maps enum name to member values
    // For numeric enums: "Color" -> {"Red" -> 0, "Green" -> 1, ...}
    // For string enums: "Direction" -> {"Up" -> "up", "Down" -> "down"}
    struct EnumValue {
        bool isString;
        int64_t numValue;
        std::string strValue;
    };
    std::map<std::string, std::map<std::string, EnumValue>> enumValues_;
    // Reverse mapping for numeric enums: "Color" -> {0 -> "Red", 1 -> "Green", ...}
    std::map<std::string, std::map<int64_t, std::string>> enumReverseMap_;

    // Deferred static property initializations (to be emitted at the start of user_main)
    struct StaticPropInit {
        std::shared_ptr<HIRValue> globalPtr;
        std::shared_ptr<HIRType> propType;
        ast::Expression* initExpr;  // Raw pointer, valid until lowering completes
    };
    std::vector<StaticPropInit> deferredStaticInits_;

    // Deferred static blocks (to be emitted at the start of user_main)
    std::vector<ast::StaticBlock*> deferredStaticBlocks_;

    // Deferred decorator invocations - classes with decorators need static init functions
    struct DeferredDecorator {
        std::string className;                      // Name of the class being decorated
        std::vector<ast::Decorator> decorators;     // Decorators to apply (in declaration order)
    };
    std::vector<DeferredDecorator> deferredDecorators_;

    // Emit deferred static initializations and static blocks
    void emitDeferredStaticInits();

    // Generate static init function for a class with decorators
    // (class, method, property, and parameter decorators)
    void generateClassDecoratorStaticInit(const std::string& className,
                                          const std::vector<ast::Decorator>& classDecorators,
                                          const std::vector<ast::NodePtr>& members);

    // Set current source line from AST node (for debug info propagation)
    void setSourceLine(ast::Node* node) {
        if (node && node->line > 0) {
            builder_.setCurrentSourceLine(static_cast<uint32_t>(node->line));
        }
    }

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
    void visitLabeledStatement(ast::LabeledStatement* node) override;
    void visitSwitchStatement(ast::SwitchStatement* node) override;
    void visitTryStatement(ast::TryStatement* node) override;
    void visitThrowStatement(ast::ThrowStatement* node) override;
    void visitImportDeclaration(ast::ImportDeclaration* node) override;
    void visitExportDeclaration(ast::ExportDeclaration* node) override;
    void visitExportAssignment(ast::ExportAssignment* node) override;
    void visitNamespaceDeclaration(ast::NamespaceDeclaration* node) override;
    void visitImportEqualsDeclaration(ast::ImportEqualsDeclaration* node) override;

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
    std::pair<bool, int64_t> constEvalEnumExpr(
        ast::Node* expr, const std::map<std::string, EnumValue>& members,
        const std::string& enumName);

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
    // Destructuring Helpers
    //==========================================================================

    // Lower object destructuring pattern: const { a, b } = obj
    void lowerObjectBindingPattern(ast::ObjectBindingPattern* pattern,
                                   std::shared_ptr<HIRValue> sourceValue);

    // Lower array destructuring pattern: const [a, b] = arr
    void lowerArrayBindingPattern(ast::ArrayBindingPattern* pattern,
                                  std::shared_ptr<HIRValue> sourceValue);

    // Lower a single binding element (for object patterns)
    void lowerBindingElement(ast::BindingElement* binding,
                             std::shared_ptr<HIRValue> sourceValue,
                             bool isObjectPattern);

    // Lower a binding element by array index
    void lowerBindingElementByIndex(ast::BindingElement* binding,
                                    std::shared_ptr<HIRValue> sourceValue,
                                    int64_t index);

    // Lower rest element: ...rest
    void lowerRestElement(ast::BindingElement* binding,
                          std::shared_ptr<HIRValue> sourceValue,
                          int64_t startIndex);

    // Box a value to Any/ptr type if needed for select instructions
    std::shared_ptr<HIRValue> boxValueIfNeeded(std::shared_ptr<HIRValue> value);

    // Force box a value - used for default params where inlining may change the actual type
    std::shared_ptr<HIRValue> forceBoxValue(std::shared_ptr<HIRValue> value);

    // Lower a MethodDefinition to a function value (for object literal methods including getters/setters)
    std::shared_ptr<HIRValue> lowerMethodDefinitionToFunction(ast::MethodDefinition* method);

    //==========================================================================
    // JSX Helpers
    //==========================================================================

    // Lower JSX attributes into a props object
    std::shared_ptr<HIRValue> lowerJsxAttributes(const std::vector<ast::NodePtr>& attributes);

    // Lower JSX children into an array
    std::shared_ptr<HIRValue> lowerJsxChildren(const std::vector<ast::ExprPtr>& children);

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

    // Pending display name for closures (set from variable assignment context)
    std::string pendingClosureDisplayName_;

    // Counter for generating unique function expression names
    int funcExprCounter_ = 0;

    // Counter for generating unique method names (for object literal methods)
    int methodCounter_ = 0;

    // Counter for generating unique class expression names
    int classExprCounter_ = 0;

    // Counter for generating unique flat object shape IDs
    uint32_t nextShapeId_ = 0;

    // Scope management
    void pushScope();
    void pushFunctionScope(HIRFunction* func);  // Push scope that marks function boundary
    void popScope();
    void defineVariable(const std::string& name, std::shared_ptr<HIRValue> value);
    void defineVariableAlloca(const std::string& name, std::shared_ptr<HIRValue> allocaPtr,
                               std::shared_ptr<HIRType> elemType);
    VariableInfo* lookupVariableInfo(const std::string& name);
    VariableInfo* lookupVariableInfoInCurrentFunction(const std::string& name);
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
    bool hasTerminator();

    //==========================================================================
    // Result Storage
    //==========================================================================

    // The result of the last expression lowered
    std::shared_ptr<HIRValue> lastValue_;
};

} // namespace ts::hir
