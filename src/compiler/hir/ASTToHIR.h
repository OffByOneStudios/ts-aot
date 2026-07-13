#pragma once

#include "../ast/AstNodes.h"
#include "HIR.h"
#include "HIRBuilder.h"
#include "../analysis/Type.h"
#include "../analysis/Module.h"
#include "../analysis/Monomorphizer.h"

#include <map>
#include <optional>
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
// 4. Creates GC-aware allocation operations (lowered to custom generational GC)
//==============================================================================

class ASTToHIR : public ast::Visitor {
public:
    ASTToHIR();
    ~ASTToHIR() override = default;

    // Flat-object shape IDs are a global runtime namespace (g_shape_table indexed
    // by id). When compiling the precompiled prelude as a SEPARATE object that is
    // linked beside a user object, its shape IDs must not collide with the user's
    // (both otherwise number from 0 → the prelude clobbers user shapes 0,1 →
    // wrong slot layouts). The Driver sets a high reserved base for the prelude.
    void setShapeIdBase(uint32_t base) { nextShapeId_ = base; }

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
    // True while lowering the body of a STATIC class member. The `_static_`
    // name convention misses the dual-version accessor stub
    // (<Class>___getter_<n>), whose `super.x` then wrongly used the instance
    // table. Set/restored at the class-method lowering sites.
    bool currentMethodIsStatic_ = false;
    // Entry module's path (first hoisted module spec) — fallback registry key
    // for CommonJS `module`/`exports` reads from functions with no
    // per-spec modulePath (e.g. user_main). See visitIdentifier.
    std::string entryModulePath_;

    // EVAL-001 §11: the program source references `eval` — main-module
    // toplevel `var` bindings become globalThis-backed (HIRModule::
    // globalObjectVars) so eval'd code and compiled code share bindings.
    bool evalTaint_ = false;

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
        // Pre-declared `let`/`const` slot holding the TDZ sentinel until its
        // declaration runs; reads are wrapped in ts_tdz_check (ReferenceError
        // on read-before-initialization).
        bool isTDZ = false;
        // Binding created for a SIMPLE identifier catch parameter. Annex B
        // B.3.5: such a binding does NOT block the B.3.3.1 var-copy of a
        // block-level function declared in the catch body — the copy-site
        // hoist-slot walk may step past it (and ONLY it).
        bool isSimpleCatchParam = false;
        // Declared with `const`: assignment expressions to this binding throw
        // TypeError (ES 13.15.2 PutValue on an immutable binding). Set at
        // declaration lowering; checked at assignment/destructuring-assignment
        // sites only (declaration/loop-binding stores are unaffected).
        bool isConst = false;
        // withDepth_ at declaration time. A binding declared OUTSIDE the
        // innermost `with` (declWithDepth < withDepth_ at the read site) is
        // shadowed by the with-object per ES 14.11 — hoisted vars and params
        // qualify; a let/const declared inside the with body does not.
        int declWithDepth = 0;
        // Function-scope slot created by the hoist prologue for a nested
        // FunctionDeclaration's name (Annex B B.3.3 var-copy target). A
        // block-level fn decl assigns THIS slot only when it exists —
        // lexical collisions suppress its creation, so the assignment
        // can't clobber a let/const of the same name.
        bool isFnHoist = false;
        // The closure that owns the cell. Kept as a single ptr for the
        // primary capturer (used by READ sites — any cell is fine since all
        // copies are kept in sync by writes). The full list of all closures
        // capturing this variable lives in additionalCaptures and is iterated
        // by WRITE sites so every cell stays current.
        std::shared_ptr<HIRValue> closurePtr = nullptr;
        int captureIndex = -1;  // Index of this variable in the closure's captures array
        // Additional capturers beyond the primary. Each entry is the alloca
        // holding the closure pointer + its capture index for this variable.
        // Populated when a second/third/... nested closure also captures this
        // var (e.g., lodash's `upperFirst` captured by many helper closures).
        std::vector<std::pair<std::shared_ptr<HIRValue>, int>> additionalCaptures;
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
        // with-scope depth at loop creation: break/continue pop the runtime
        // with-stack down to this level (mirrors tryDepth_/PopHandler).
        int withDepth = 0;
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

    // Try block depth: tracks how many exception handlers are active.
    // return/break/continue inside try blocks must emit PopHandler for each
    // active handler to prevent leaked handlers pointing to destroyed frames.
    int tryDepth_ = 0;

    // Direct-eval-in-parameter-initializer context (ES2025 19.2.1.3 step 5.d).
    // paramEvalCtxFlags_: value each visitFunction* site computes for its own
    // params (bit0 = param-init, bit1 = an 'arguments' binding lies on the
    // eval's lexEnv->varEnv walk: any non-arrow form, or an arrow with a
    // parameter named 'arguments'). activeEvalFlags_/evalFlagsOwner_: armed by
    // bindOneParameter/extractDestructuringForParam only while lowering a
    // default expression; the eval() lowering consumes them only when
    // evalFlagsOwner_ == currentFunction_, so nested function bodies inside a
    // default expression never inherit the context.
    int paramEvalCtxFlags_ = 0;
    int activeEvalFlags_ = 0;
    HIRFunction* evalFlagsOwner_ = nullptr;

    // Names assigned inside ANY `with` body (program-wide): their declaration
    // slots stay Any-typed (see collectWithPoisonNames in Internal.h).
    std::set<std::string> withPoisonedVars_;

    // Lexical private-name scope (ES PrivateEnvironment): innermost enclosing
    // class DECLARING a #name owns it. Pushed per class lowering with the
    // class's declared private names; resolvePrivateName qualifies "#x" ->
    // "#x@<classId>" so same-named privates of nested classes get DISTINCT
    // storage/brands (the shadowed-by-field-on-nested-class family).
    // fields mangle to per-class keys; methods/accessors keep their legacy
    // plain keys (their prototype/vtable installs are not class-qualified
    // yet) but still SHADOW outer declarations in the walk.
    struct PrivateClassCtx {
        std::string id;
        std::set<std::string> fields;    // instance/static PropertyDefinitions
        std::set<std::string> others;    // methods + accessors (key resolution)
        std::set<std::string> methods;   // plain methods only — writes TypeError
        std::set<std::string> getters;   // accessor names WITH a getter half
        std::set<std::string> accessors; // accessor names (getter or setter)
    };
    std::vector<PrivateClassCtx> privateClassStack_;
    // Lexical private scope captured at class-visit time, keyed by HIR class
    // name — restored around Monomorphizer SPEC method-body lowering (which
    // runs outside the class visit, so the live stack is empty there).
    std::map<std::string, std::vector<PrivateClassCtx>> classPrivSnapshots_;
    // "#x" -> "#x@Class" when the innermost declaring class declares it as a
    // FIELD; plain "#x" when declared as a method/accessor (legacy keys) or
    // unresolved. Non-private names pass through.
    // ES PrivateGet: a private ACCESSOR without a getter half throws on
    // read (get-access-of-missing-private-getter). Checked at the innermost
    // class that declares the name.
    bool privateIsSetterOnly(const std::string& name) {
        if (name.empty() || name[0] != '#') return false;
        for (auto it = privateClassStack_.rbegin();
             it != privateClassStack_.rend(); ++it) {
            if (it->fields.count(name) || it->others.count(name))
                return it->accessors.count(name) && !it->getters.count(name);
        }
        return false;
    }

    std::string resolvePrivateName(const std::string& name) {
        if (name.empty() || name[0] != '#') return name;
        for (auto it = privateClassStack_.rbegin();
             it != privateClassStack_.rend(); ++it) {
            if (it->fields.count(name)) return name + "@" + it->id;
            if (it->others.count(name)) return name + "@" + it->id;
        }
        return name;  // unresolved (invalid code / static-deferred residue)
    }
    // Write-site key. FIELDS use the hidden storage form "#x@Class"
    // (raw data slot). ACCESSORS/METHODS use the plain qualified name — the
    // runtime set path dispatches "__setter_<key>" accessors from the plain
    // key, and a  prefix defeats that (compound/destructuring writes to
    // a private accessor must call the setter per PutValue).
    std::string resolvePrivateKey(const std::string& name) {
        if (name.empty() || name[0] != '#') return name;
        for (auto it = privateClassStack_.rbegin();
             it != privateClassStack_.rend(); ++it) {
            if (it->fields.count(name))
                return std::string(1, static_cast<char>(0x01)) + name + "@" + it->id;
            if (it->others.count(name)) return name + "@" + it->id;
        }
        return std::string(1, static_cast<char>(0x01)) + name;  // unresolved: legacy
    }
    // 'f' field, 'm' plain method (assignment/compound write -> TypeError per
    // PutValue on a method Private Name), 'a' accessor, 0 unresolved.
    char privateMemberKindOf(const std::string& name) {
        if (name.empty() || name[0] != '#') return 0;
        for (auto it = privateClassStack_.rbegin();
             it != privateClassStack_.rend(); ++it) {
            if (it->fields.count(name)) return 'f';
            if (it->methods.count(name)) return 'm';
            if (it->others.count(name)) return 'a';
        }
        return 0;
    }

    // GEN-001 Stage 6: parallel stack of enclosing try scopes — the function
    // they belong to plus their catch-dispatch block (visitTryStatement's
    // exceptionDest). Pushed/popped exactly where tryDepth_ changes. Yields
    // copy the entries tagged with the CURRENT function into the HIR
    // instruction's tryCatchTargets so HIRToLLVM can pop-balance suspend
    // edges and re-arm handlers on resume. Tagging by function (instead of
    // save/restore at every nested-function lowering site) filters out scopes
    // that belong to an enclosing function while a nested function body is
    // lowered inline.
    std::vector<std::pair<HIRFunction*, HIRBlock*>> tryScopeStack_;

    // Class context - tracks when we're inside a class body
    HIRClass* currentClass_ = nullptr;

    // Class expression tracking - maps variable names to class names for class expressions
    // E.g., "const MyClass = class { ... }" maps "MyClass" -> "__class_expr_0" or generated name
    std::map<std::string, std::string> variableToClassName_;
    // Phase 9c-i: cache mapping AST class expression nodes to their registered
    // HIRClass. Pre-scan in pass 1 of lower() registers all top-level class
    // expressions early so visitNewExpression in function bodies can find them.
    // Without this cache, the second invocation (during normal var-decl
    // lowering) would create a duplicate class with a different __anon_class_N
    // name.
    std::map<ast::ClassExpression*, HIRClass*> astClassExprToHIRClass_;

    // Last generated class name from visitClassExpression (used by visitVariableDeclaration)
    std::string lastGeneratedClassName_;

    // Static property globals - maps "ClassName_static_propName" to (globalPtr, type)
    std::map<std::string, std::pair<std::shared_ptr<HIRValue>, std::shared_ptr<HIRType>>> staticPropertyGlobals_;

    // Module-scoped variable declarations from imported modules
    // Maps variable name to the AST VariableDeclaration node (for lazy initialization)
    std::map<std::string, ast::VariableDeclaration*> moduleVarDecls_;
    // Per-module set of module-scoped variable names that have been promoted to globals.
    // Maps var name -> set of module paths that define it.
    // This prevents cross-module name collisions (e.g., `var next` in module A
    // should not shadow a local `function next()` in module B).
    std::map<std::string, std::set<std::string>> moduleGlobalVarsByModule_;
    // Module-TOPLEVEL var/function declaration names (incl. initializer-less
    // `var x;`): the strict-unresolvable-assign check exempts DECLARED names
    // written from class methods/inner functions (sweep #33 regression).
    std::set<std::string> moduleToplevelDeclaredNames_;
    // Module globals accessed by inner (nested) functions -- the defining function
    // must read/write these from __modvar_ globals, not local allocas
    std::map<std::string, std::set<std::string>> moduleGlobalsUsedByInnerByModule_;

    // Helper: check if name is a module global for the current module
    bool isModuleGlobalVar(const std::string& name) const {
        auto it = moduleGlobalVarsByModule_.find(name);
        if (it == moduleGlobalVarsByModule_.end()) return false;
        return it->second.count(currentModulePath_) > 0;
    }
    // Helper: when `name` is a module global of exactly ONE module, return its
    // __modvar_ global name (empty string otherwise). Used by identifier
    // resolution from spec-lowered function bodies whose currentModulePath_
    // differs from the owning module (anonymous class-expression methods
    // referencing the assigned binding: `var C = class { static m() { C.#x } }`).
    // The single-owner requirement keeps the documented per-module isolation
    // (a same-named var in another module must not be redirected).
    std::string uniqueModuleGlobalName(const std::string& name) const {
        auto it = moduleGlobalVarsByModule_.find(name);
        if (it == moduleGlobalVarsByModule_.end() || it->second.size() != 1)
            return std::string();
        const std::string& ownerPath = *it->second.begin();
        if (ownerPath.empty()) return "__modvar_" + name;
        std::hash<std::string> hasher;
        auto hash = hasher(ownerPath) % 999999;
        return "__modvar_" + name + "_m" + std::to_string(hash);
    }
    // Helper: check if name is used by an inner function in the current module
    bool isModuleGlobalUsedByInner(const std::string& name) const {
        auto it = moduleGlobalsUsedByInnerByModule_.find(name);
        if (it == moduleGlobalsUsedByInnerByModule_.end()) return false;
        return it->second.count(currentModulePath_) > 0;
    }
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
        // Constructor-object mirror: install the initialized value as an own
        // property of the class constructor closure so static fields are
        // reachable through an alias / dynamic key / passed reference, not
        // only through the literal-class-name fast path.
        std::string ctorName;   // class constructor closure name (e.g. C_constructor)
        std::string fieldName;  // static field name (e.g. sf)
        // For a COMPUTED static field name (`static [expr] = v`), the key
        // expression to evaluate at init time and install the value under on the
        // constructor (fieldName is the "[computed]" placeholder, unusable).
        ast::Node* computedNameNode = nullptr;
        // Private-name scope captured at defer time so `this.#x` / `C.#x`
        // inside the initializer resolves to the class-qualified key when
        // lowered later (outside the class visit).
        std::vector<PrivateClassCtx> privSnapshot;
    };
    std::vector<StaticPropInit> deferredStaticInits_;

    // True while lowering the operand of a `typeof` unary expression. An
    // unresolvable bare identifier under `typeof` yields "undefined" (ECMA-262
    // 13.5.1.1) and must NOT throw the ReferenceError that a normal read would.
    bool inTypeofOperand_ = false;

    // File-level "use strict" (leading directive prologue of the Program).
    // Property assignments in strict code lower to the *_strict runtime
    // entries, which throw TypeError on a blocked write (ES 13.15.2 PutValue
    // with throw = true) instead of silently no-oping.
    bool strictCode_ = false;
    // True when lowering a "use fast" file: convertTypeFromString only maps the
    // fixed-width numeric aliases (i32/f64/...) then, keeping non-fast files a
    // strict TS subset. Set from Program::isFast in lower().
    bool fastCode_ = false;

    // Lexical `with` nesting depth (ES 14.11). Entries are pushed on the
    // runtime with-stack by the with-block lowering; return/break/continue
    // emit ts_with_pop_n to unwind (mirrors tryDepth_).
    int withDepth_ = 0;
    // True when lowering code LEXICALLY inside a `with` body — including
    // nested function bodies (a closure defined inside `with` carries the
    // object environment in its scope chain; the runtime with-stack is
    // still live when it's called synchronously). withDepth_ itself resets
    // per function (it drives pop_n unwinding, which must stay
    // function-local); this flag drives the identifier read/write routing.
    bool withLexical_ = false;
    bool withScopeActive() const { return withDepth_ > 0 || withLexical_; }
    // True when the CURRENT function's prologue emitted ts_with_enter_fn
    // (a function lexically inside a with restores its captured object
    // environment at call time). Return lowering pairs it with
    // ts_with_exit_fn. Saved/restored per function like withDepth_.
    bool withEnvEntered_ = false;

    // Deferred static blocks (to be emitted at the start of user_main)
    std::vector<std::pair<ast::StaticBlock*, std::vector<PrivateClassCtx>>> deferredStaticBlocks_;

    // Deferred class prototype installs. Class declarations are processed
    // in a pre-pass with currentFunction_ == null, so we cannot emit IR
    // for the `E.prototype = {__getter_<key>: ..., method: ...}` setup
    // at the declaration site. Push the HIRClass here and emit the
    // trailer IR at the start of user_main / __synthetic_user_main.
    std::vector<HIRClass*> deferredClassPrototypes_;
    // Emit ONE class's full setup (prototype, methods, heritage links,
    // computed accessors) at the CURRENT insert point. Used by the deferred
    // flush (module-level classes) and INLINE at the class statement's
    // source position for classes in nested function bodies — their setup
    // side effects (heritage eval, computed keys) must run inside the
    // enclosing function so try/catch sees them (ES ClassDefinitionEvaluation).
    void emitSingleClassSetup(HIRClass* hirClass, bool valueResolveHeritage = false);
    void emitDeferredStaticInitsTail();  // static blocks etc. (split from flush)

    // Deferred decorator invocations - classes with decorators need static init functions
    struct DeferredDecorator {
        std::string className;                      // Name of the class being decorated
        std::vector<ast::Decorator> decorators;     // Decorators to apply (in declaration order)
    };
    std::vector<DeferredDecorator> deferredDecorators_;

    // Emit deferred static initializations and static blocks
    void emitDeferredStaticInits();

    // Install a class's computed-name accessors (get/set [expr]) onto the
    // prototype (instance) / constructor object (static).
    void emitComputedAccessorInstalls(HIRClass* hirClass,
                                      std::shared_ptr<HIRValue> proto,
                                      std::shared_ptr<HIRValue> ctorVal);

    // Generate static init function for a class with decorators
    // (class, method, property, and parameter decorators)
    void generateClassDecoratorStaticInit(const std::string& className,
                                          const std::vector<ast::Decorator>& classDecorators,
                                          const std::vector<ast::NodePtr>& members);

    // Set current source location from AST node (for debug info and coverage)
    void setSourceLine(ast::Node* node) {
        if (node && node->line > 0) {
            uint16_t fileIdx = getOrCreateFileIndex(node->sourceFile);
            builder_.setCurrentSourceLoc(fileIdx,
                static_cast<uint32_t>(node->line),
                static_cast<uint16_t>(node->column));
        }
    }

    uint16_t getOrCreateFileIndex(const std::string& path) {
        if (path.empty()) return 0;
        auto& files = module_->sourceFiles;
        for (uint16_t i = 0; i < files.size(); ++i) {
            if (files[i] == path) return i;
        }
        files.push_back(path);
        return static_cast<uint16_t>(files.size() - 1);
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
    // Compute a class method's unique global symbol name + its registration key.
    // Shared by the class-declaration and class-expression lowering paths so the
    // two naming schemes never drift (the "works as class decl, broken as class
    // expression" hazard). Returns the func name; sets outMethodKey.
    std::string computeClassMethodFuncName(const std::string& className,
                                           ast::MethodDefinition* methodDef,
                                           bool isComputedAccessor,
                                           int& computedAccessorSeq,
                                           std::string& outMethodKey);
    // Install a class method/accessor/static member on a receiver with the spec
    // method descriptor {writable, configurable, NON-enumerable} via
    // ts_object_set_method (NOT createSetPropStatic, which is enumerable).
    // Applies the private-name "\x01#"-prefix remap. Shared by the deferred
    // declaration path and the class-expression install trailers so a class
    // expression's methods are non-enumerable like a class declaration's.
    void installClassMember(std::shared_ptr<HIRValue> recv,
                            const std::string& key,
                            std::shared_ptr<HIRValue> closure);
    // Resolve the symbol of the COMPLETE method body for `methodKey`. For get/set
    // accessors, hirClass->methods may hold an empty module-level placeholder
    // whose real (monomorphized) body lives under the vtable entry's mangledName;
    // prefer that. Returns the module function symbol to install on the prototype.
    std::string completeMethodSymbol(HIRClass* hirClass, const std::string& methodKey,
                                     HIRFunction* fallback, bool isStatic = false);
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

    // Lower a destructuring-ASSIGNMENT pattern (`[a,b]=src` / `({a,b:t}=src)`)
    // given the already-lowered source value. Recursive: nested patterns
    // (`({a:{b}}=o)`, `[{x},[y]]=a`) re-enter this for the inner pattern.
    void destructureAssignmentPattern(ast::Expression* lhs,
                                      std::shared_ptr<HIRValue> rhs);

    // Assign a value to a bare variable by name (the identifier/shorthand
    // target path shared by both destructuring-assignment branches).
    void assignDestructureName(const std::string& name,
                               std::shared_ptr<HIRValue> value);

    // Set an instance field on `thisValue` to `initVal`. Handles computed
    // property names (`["a"+"b"] = v`) via SetPropDynamic; static-name fields
    // via SetPropStatic. `propDef` is a non-static class field.
    void emitInstanceFieldSet(std::shared_ptr<HIRValue> thisValue,
                              ast::PropertyDefinition* propDef,
                              std::shared_ptr<HIRValue> initVal);

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
    // Parameter Binder Helpers (Strategy B Phase 6)
    //==========================================================================

    // Bind a single function parameter: build the param HIRValue, emit the
    // default-value branch if astParam->initializer is set, register the
    // variable in the current scope (alloca-based or direct value).
    //
    // hirParamIndex is the index into func->params (which may differ from the
    // AST parameter index when the function has prepended __closure__ slots).
    // astParam may be nullptr for synthetic params (e.g., __closure__ itself).
    // useAlloca=true uses defineVariableAlloca (params can be reassigned);
    // useAlloca=false uses defineVariable (direct HIRValue, used by methods).
    // ECMA-262 parameter TDZ: pre-seed Any-typed named params with the TDZ
    // sentinel when any default exists (defaults reading later/self params
    // throw ReferenceError). Call BEFORE the bindOneParameter loop.
    void preseedParamTDZ(HIRFunction* func,
                         const std::vector<std::unique_ptr<ast::Parameter>>& astParams);
    void bindOneParameter(HIRFunction* func,
                          size_t hirParamIndex,
                          ast::Parameter* astParam,
                          bool useAlloca);

    // Emit destructuring extraction for a parameter that has a binding pattern.
    // hirParamIndex is the index into func->params; pattern is either an
    // ObjectBindingPattern or ArrayBindingPattern from the original AST.
    void extractDestructuringForParam(HIRFunction* func,
                                      size_t hirParamIndex,
                                      ast::ObjectBindingPattern* objPattern,
                                      ast::ArrayBindingPattern* arrPattern,
                                      ast::Node* defaultInitializer = nullptr);

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

    // "use fast" struct value semantics (docs/design/use-fast.md): true iff the
    // HIR value is an instance of a `struct` class. maybeCloneStruct wraps a
    // struct value in ts_flat_object_clone when it is read from an lvalue
    // (identifier / member access) into a value context — assignment, binding,
    // argument, or return — so structs copy instead of aliasing. A fresh
    // new/call already yields an independent value and is not cloned.
    bool isStructValue(const std::shared_ptr<HIRValue>& v);
    std::shared_ptr<HIRValue> maybeCloneStruct(std::shared_ptr<HIRValue> v,
                                               ast::Expression* src);

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
    void broadcastCaptureWrite(VariableInfo* info, std::shared_ptr<HIRValue> newValue);
    // SMELL-002: shared MakeClosure trailer — the optional with-scope bind
    // (ts_with_bind_fn when lexically inside a `with`) plus the
    // capture-marking loop that registers the closure cell on every
    // captured outer variable (isCapturedByNested/additionalCaptures so
    // broadcastCaptureWrite reaches all capturing closures). Was copy-
    // pasted at 4 sites (fn-decl/arrow/fn-expr/method) with drift.
    void finishClosure(const std::string& funcName,
                       std::shared_ptr<HIRValue> closureVal,
                       const std::vector<std::pair<std::string,
                           std::shared_ptr<HIRType>>>& innerCaptures,
                       bool emitWithBind = true);
    // ES 14.11: inside a `with` body wrap a statically-resolved identifier
    // READ (already in lastValue_) in ts_with_shadow_or so the with-object
    // shadows bindings declared outside the with. No-op outside with bodies,
    // under typeof, for `undefined`, or for bindings declared inside the
    // with body itself (vinfo->declWithDepth == withDepth_).
    void maybeWithShadowWrap(const std::string& name, VariableInfo* vinfo);

    // Closure capture helpers
    // Looks up a variable and determines if it's captured from an outer function
    // Returns true if the variable crosses a function boundary
    bool isCapturedVariable(const std::string& name, size_t* outScopeIndex = nullptr);

    // Register a captured variable for the current function being lowered
    void registerCapture(const std::string& name, std::shared_ptr<HIRType> type, size_t scopeIndex);

    // Get all captures for the current nested function
    const std::vector<CaptureInfo>& getPendingCaptures() const { return pendingCaptures_; }
    void clearPendingCaptures() { pendingCaptures_.clear(); }

    // Mutual recursion fix: after all inner function declarations are processed,
    // patch stale closure cells where one sibling captured another that didn't
    // exist yet at capture time.
    struct InnerFuncClosureInfo {
        std::string funcName;
        std::shared_ptr<HIRValue> closureValue;
        std::vector<std::pair<std::string, int>> captureNamesAndIndices;
    };
    std::vector<InnerFuncClosureInfo> innerFuncClosures_;
    void emitMutualRecursionFixup();

    //==========================================================================
    // SMELL-002: RAII save/reset/restore of ALL per-function lowering state.
    // One definition replaces the 10 hand-copied visitor prologues, which had
    // drifted: lowerMethodDefinitionToFunction lost innerFuncClosures_, the
    // class-inline/defaultCtor/decorator sites never cleared the loop/label/
    // switch stacks or pendingCaptures_, and NO site cleared
    // breakTargetStack_/pendingLabel_ (a `break` in a nested function could
    // target the OUTER function's switch block).
    //
    // Usage: hold in a std::optional at function-body entry and reset() at
    // the exact old restore point — code after it runs in the OUTER context.
    // currentFunction_/currentBlock_/scopes stay site-managed (bespoke
    // choreography per visitor). strictCode_ is SAVED but not reset here:
    // strictness inherits lexically; sites apply their own "use strict"
    // directive scan after construction.
    //==========================================================================
    class FunctionLoweringScope {
    public:
        explicit FunctionLoweringScope(ASTToHIR& l)
            : l_(l),
              tryDepth_(l.tryDepth_), withDepth_(l.withDepth_),
              withLexical_(l.withLexical_), withEnvEntered_(l.withEnvEntered_),
              strictCode_(l.strictCode_),
              pendingCaptures_(std::move(l.pendingCaptures_)),
              innerFuncClosures_(std::move(l.innerFuncClosures_)),
              loopStack_(std::move(l.loopStack_)),
              labeledLoops_(std::move(l.labeledLoops_)),
              switchStack_(std::move(l.switchStack_)),
              breakTargetStack_(std::move(l.breakTargetStack_)),
              pendingLabel_(std::move(l.pendingLabel_)) {
            l.tryDepth_ = 0;
            l.withDepth_ = 0;
            l.withLexical_ = l.withLexical_ || withDepth_ > 0;
            l.withEnvEntered_ = false;
            l.pendingCaptures_.clear();
            l.innerFuncClosures_.clear();
            l.loopStack_ = {};
            l.labeledLoops_.clear();
            l.switchStack_ = {};
            l.breakTargetStack_ = {};
            l.pendingLabel_.clear();
        }
        ~FunctionLoweringScope() {
            l_.tryDepth_ = tryDepth_;
            l_.withDepth_ = withDepth_;
            l_.withLexical_ = withLexical_;
            l_.withEnvEntered_ = withEnvEntered_;
            l_.strictCode_ = strictCode_;
            l_.pendingCaptures_ = std::move(pendingCaptures_);
            l_.innerFuncClosures_ = std::move(innerFuncClosures_);
            l_.loopStack_ = std::move(loopStack_);
            l_.labeledLoops_ = std::move(labeledLoops_);
            l_.switchStack_ = std::move(switchStack_);
            l_.breakTargetStack_ = std::move(breakTargetStack_);
            l_.pendingLabel_ = std::move(pendingLabel_);
        }
        FunctionLoweringScope(const FunctionLoweringScope&) = delete;
        FunctionLoweringScope& operator=(const FunctionLoweringScope&) = delete;

    private:
        ASTToHIR& l_;
        int tryDepth_;
        int withDepth_;
        bool withLexical_;
        bool withEnvEntered_;
        bool strictCode_;
        std::vector<CaptureInfo> pendingCaptures_;
        std::vector<InnerFuncClosureInfo> innerFuncClosures_;
        std::stack<LoopContext> loopStack_;
        std::map<std::string, LoopContext> labeledLoops_;
        std::stack<SwitchContext> switchStack_;
        std::stack<HIRBlock*> breakTargetStack_;
        std::string pendingLabel_;
    };

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
