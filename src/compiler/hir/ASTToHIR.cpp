#include "ASTToHIR_Internal.h"

namespace ts::hir {


//==============================================================================
// Constructor / Entry Point
//==============================================================================

ASTToHIR::ASTToHIR() : builder_(nullptr) {}

std::unique_ptr<HIRModule> ASTToHIR::lower(ast::Program* program, const std::string& moduleName) {
    module_ = std::make_unique<HIRModule>(moduleName);
    module_->sourcePath = program->sourceFile;
    builder_ = HIRBuilder(module_.get());
    // File-level "use strict": stamped by the parser (the Monomorphizer moves
    // body statements into function specs before this runs, so the directive
    // prologue is no longer visible in program->body here).
    strictCode_ = program->isStrict;

    valueCounter_ = 0;
    blockCounter_ = 0;
    scopes_.clear();
    pushScope();  // Global scope

    // Check if we need a module init function for top-level executable code
    // (VariableDeclarations with initializers, ExpressionStatements, etc.)
    bool needsModuleInit = false;
    for (auto& stmt : program->body) {
        std::string kind = stmt->getKind();
        if (kind == "VariableDeclaration" || kind == "ExpressionStatement" || kind == "BlockStatement") {
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
    module_->sourcePath = program->sourceFile;
    builder_ = HIRBuilder(module_.get());
    strictCode_ = program->isStrict;  // stamped by the parser (see above)

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

    // Determine the main source file for distinguishing imported modules.
    // The main file's statements come LAST in program->body (after all imports).
    // So we scan backwards to find the last unique sourceFile.
    mainSourceFile_.clear();
    for (auto it = program->body.rbegin(); it != program->body.rend(); ++it) {
        if (!(*it)->sourceFile.empty()) {
            mainSourceFile_ = (*it)->sourceFile;
            break;
        }
    }

    // Scan for module-scoped VariableDeclarations from imported modules.
    // Functions from imported modules may reference these variables (e.g., defaultOptions
    // in benchmark.ts referenced by benchmark()). We register them as module globals
    // so they can be resolved in visitIdentifier.
    moduleVarDecls_.clear();
    moduleGlobalVarsByModule_.clear();
    // Scan module init specializations for VariableDeclarations.
    // The Monomorphizer moves VariableDeclarations from imported modules into
    // __module_init_<hash> specialization functions. We need to find these and
    // register them as module globals so other functions from the same module
    // can reference them via LoadGlobal/StoreGlobal.
    for (const auto& spec : specializations) {
        if (spec.originalName.find("__module_init_") != 0) continue;
        auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node);
        if (!funcNode) continue;
        // Set currentModulePath_ so modVarName() generates unique globals per module
        currentModulePath_ = spec.modulePath;
        // Note: We include all module init functions (including the main file)
        // because file-level variables need to be shared across functions.
        // Helper to register a single name as a module global
        auto registerModuleGlobalName = [&](const std::string& name, std::shared_ptr<HIRType> globalType) {
            // Skip compiler-synthesized `__module_init_*`, `__synthetic_*`, and
            // `exports` (handled separately). The CJS-style globals `__filename`
            // and `__dirname` (injected by the Monomorphizer for user modules)
            // ARE registered as module globals so inner functions can read them
            // via @__modvar_X. Without this exemption, user_main reading
            // `__filename` would see undefined.
            if (name == "exports") return;
            if (name.find("__") == 0 && name != "__filename" && name != "__dirname") return;
            moduleGlobalVarsByModule_[name].insert(currentModulePath_);
            module_->globals[modVarName(name)] = globalType;
        };

        // Helper to extract all binding names from a destructuring pattern
        std::function<void(ast::Node*, std::shared_ptr<HIRType>)> registerBindingNames;
        registerBindingNames = [&](ast::Node* node, std::shared_ptr<HIRType> globalType) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(node)) {
                registerModuleGlobalName(ident->name, globalType);
            } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(node)) {
                for (auto& elem : objPat->elements) {
                    if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
                        registerBindingNames(binding->name.get(), globalType);
                    }
                }
            } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(node)) {
                for (auto& elem : arrPat->elements) {
                    if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
                        registerBindingNames(binding->name.get(), globalType);
                    }
                }
            }
        };

        // Helper lambda to register a VariableDeclaration as a module global
        auto registerModuleVar = [&](ast::VariableDeclaration* varDecl) {
            // Infer type from initializer to preserve Object vs Any distinction.
            // This is critical for method dispatch: without it, object literal methods
            // like "add" or "info" collide with Set.add() or console.info().
            auto globalType = HIRType::makeAny();
            if (varDecl->initializer) {
                auto kind = varDecl->initializer->getKind();
                if (kind == "ObjectLiteralExpression") {
                    globalType = HIRType::makeObject();
                } else if (kind == "ArrayLiteralExpression") {
                    globalType = HIRType::makeArray(HIRType::makeAny());
                }
            }

            if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                // Simple variable: const x = ...
                // Skip compiler-synthesized names but allow the CJS-style globals
                // __filename and __dirname (injected by the Monomorphizer).
                if (ident->name == "exports") return;
                if (ident->name.find("__") == 0 &&
                    ident->name != "__filename" && ident->name != "__dirname") return;
                moduleVarDecls_[ident->name] = varDecl;
                registerModuleGlobalName(ident->name, globalType);
            } else {
                // Destructuring pattern: const { a, b } = ... or const [a, b] = ...
                // For destructured require(), each extracted variable is Any type
                registerBindingNames(varDecl->name.get(), HIRType::makeAny());
            }
        };

        // Recursively walk into block-like statements so `var` declarations
        // inside any nested block hoist to module-global scope per ECMA-262.
        // Stops at function/method boundaries (those are their own scopes).
        std::function<void(ast::Statement*)> walkForVars;
        walkForVars = [&](ast::Statement* s) {
            if (!s) return;
            if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(s)) {
                // Only `var` (and legacy `function` declarations) hoist;
                // `let`/`const` are block-scoped per ECMA-262.
                if (varDecl->varKind == ast::VarKind::Var) {
                    registerModuleVar(varDecl);
                }
                return;
            }
            if (auto* block = dynamic_cast<ast::BlockStatement*>(s)) {
                for (auto& inner : block->statements) {
                    walkForVars(dynamic_cast<ast::Statement*>(inner.get()));
                }
                return;
            }
            if (auto* ifSt = dynamic_cast<ast::IfStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(ifSt->thenStatement.get()));
                walkForVars(dynamic_cast<ast::Statement*>(ifSt->elseStatement.get()));
                return;
            }
            if (auto* w = dynamic_cast<ast::WhileStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(w->body.get()));
                return;
            }
            if (auto* f = dynamic_cast<ast::ForStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(f->initializer.get()));
                walkForVars(dynamic_cast<ast::Statement*>(f->body.get()));
                return;
            }
            if (auto* fi = dynamic_cast<ast::ForInStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(fi->initializer.get()));
                walkForVars(dynamic_cast<ast::Statement*>(fi->body.get()));
                return;
            }
            if (auto* fo = dynamic_cast<ast::ForOfStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(fo->initializer.get()));
                walkForVars(dynamic_cast<ast::Statement*>(fo->body.get()));
                return;
            }
            if (auto* lab = dynamic_cast<ast::LabeledStatement*>(s)) {
                walkForVars(dynamic_cast<ast::Statement*>(lab->statement.get()));
                return;
            }
            if (auto* sw = dynamic_cast<ast::SwitchStatement*>(s)) {
                for (auto& clause : sw->clauses) {
                    if (auto* cc = dynamic_cast<ast::CaseClause*>(clause.get())) {
                        for (auto& cs : cc->statements) walkForVars(dynamic_cast<ast::Statement*>(cs.get()));
                    } else if (auto* dc = dynamic_cast<ast::DefaultClause*>(clause.get())) {
                        for (auto& cs : dc->statements) walkForVars(dynamic_cast<ast::Statement*>(cs.get()));
                    }
                }
                return;
            }
            if (auto* tr = dynamic_cast<ast::TryStatement*>(s)) {
                for (auto& tb : tr->tryBlock) walkForVars(dynamic_cast<ast::Statement*>(tb.get()));
                if (tr->catchClause) {
                    for (auto& cb : tr->catchClause->block) walkForVars(dynamic_cast<ast::Statement*>(cb.get()));
                }
                for (auto& fb : tr->finallyBlock) walkForVars(dynamic_cast<ast::Statement*>(fb.get()));
                return;
            }
            // Don't recurse into FunctionDeclaration, FunctionExpression,
            // ArrowFunction, ClassDeclaration — those are own-scope boundaries.
        };

        for (auto& stmt : funcNode->body) {
            if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                // Function declarations must also be registered as module globals.
                // Without this, functions captured by closures in the same module
                // use closure cells, which fail when the captured function is declared
                // after the capturing function (capture gets null due to source ordering).
                if (!funcDecl->name.empty() && funcDecl->name.find("__") != 0) {
                    moduleGlobalVarsByModule_[funcDecl->name].insert(currentModulePath_);
                    module_->globals[modVarName(funcDecl->name)] = HIRType::makeAny();
                }
            } else if (auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                // Top-level let/const are module-scoped (their "enclosing
                // block" IS the module). They need to be __modvar_ globals so
                // inner functions reading them across the module_init →
                // user_main → callback path see the same storage. Without
                // this, top-level `const stats = {...}` would be scalar-
                // replaced inside __module_init and inner reads from
                // user_main get NANBOX_UNDEFINED. (walkForVars below
                // intentionally still skips let/const at nested-block depth
                // since those are block-scoped per ECMA-262.)
                registerModuleVar(varDecl);
            } else {
                walkForVars(dynamic_cast<ast::Statement*>(stmt.get()));
            }
        }
    }

    // Phase 9c-i: pre-register top-level class expressions assigned to
    // variables (`const X = class { ... }`). Without this, function bodies
    // visited in the second pass below see no class registered for X — they
    // fall through visitNewExpression's "Unknown class" branch.
    //
    // Calling visitClassExpression here registers the HIRClass, shape,
    // constructor, and methods. The trailer (loadFunction + prototype setup)
    // is skipped because currentFunction_ is null at this point; the second
    // invocation from visitVariableDeclaration during normal lowering hits
    // the astClassExprToHIRClass_ cache and emits the trailer in the correct
    // function context.
    //
    // Critically, this pre-pass MUST run AFTER moduleGlobalVarsByModule_ is
    // populated (the var-scan loop above): visitClassExpression eagerly
    // emits method bodies, and writes inside those bodies need to see
    // `isModuleGlobalVar(name)` as true so they take the StoreGlobal path
    // instead of falling through to a method-local alloca that's invisible
    // to module_init's reads. Class-expression methods have no spec-loop
    // entry (Monomorphizer doesn't specialize anonymous class methods), so
    // this is the ONLY emission of those bodies.
    for (auto& stmt : program->body) {
        auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
        if (!varDecl || !varDecl->initializer) continue;
        auto* classExpr = dynamic_cast<ast::ClassExpression*>(varDecl->initializer.get());
        if (!classExpr) continue;
        // ECMA-262 NamedEvaluation: `var B = class {}` gives the anonymous class
        // the binding name (B.name === "B"). This pre-scan is the only emission
        // of top-level class-expression bodies (visitVariableDeclaration's copy
        // is dropped by the Monomorphizer), so set the inferred display name
        // here. A class expression with its own name (`var C = class Named {}`)
        // keeps "Named".
        std::string savedPCDN = pendingClosureDisplayName_;
        if (classExpr->name.empty()) {
            if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                pendingClosureDisplayName_ = ident->name;
            }
        }
        visitClassExpression(classExpr);
        pendingClosureDisplayName_ = savedPCDN;
        if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
            variableToClassName_[ident->name] = lastGeneratedClassName_;
        }
    }

    // Pre-pass: create HIRClass objects for imported classes that aren't in the main program.
    // The first pass only processes ClassDeclarations from the main file's AST, so classes
    // defined in imported modules don't get HIRClass objects. We detect these from the
    // specializations (which include methods from all classes).
    // We use the ClassDeclaration's sourceFile to determine if a class is from the main
    // program (will be handled by visitClassDeclaration) or from an imported module.
    std::string mainSourceFile;
    if (!program->body.empty() && !program->body[0]->sourceFile.empty()) {
        mainSourceFile = program->body[0]->sourceFile;
    }

    std::set<std::string> classesCreatedFromSpecs;
    for (const auto& spec : specializations) {
        if (!spec.classType) continue;
        auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
        if (!classType || !classType->node) continue;
        std::string className = classType->name;

        // Skip classes from the main source file - they will be handled by
        // visitClassDeclaration during normal statement processing
        if (!mainSourceFile.empty() && classType->node->sourceFile == mainSourceFile) continue;

        // Check if this class already exists (from the first pass / main file)
        bool alreadyExists = false;
        for (auto& cls : module_->classes) {
            if (cls->name == className) {
                alreadyExists = true;
                break;
            }
        }
        if (alreadyExists) continue;
        if (classesCreatedFromSpecs.count(className)) continue;
        classesCreatedFromSpecs.insert(className);

        // Create HIRClass for this imported class
        auto* hirClass = builder_.createClass(className);
        if (!hirClass) continue;

        // Build shape from the ClassDeclaration's property definitions
        ast::ClassDeclaration* classDecl = classType->node;
        auto shape = std::make_shared<HIRShape>();
        shape->className = className;
        uint32_t propertyOffset = 0;

        // Handle base class
        if (!classDecl->baseClass.empty()) {
            for (auto& cls : module_->classes) {
                if (cls->name == classDecl->baseClass) {
                    hirClass->baseClass = cls.get();
                    break;
                }
            }
            if (hirClass->baseClass && hirClass->baseClass->shape) {
                auto baseShape = hirClass->baseClass->shape;
                shape->parent = baseShape.get();
                for (const auto& [name, offset] : baseShape->propertyOffsets) {
                    shape->propertyOffsets[name] = offset;
                }
                for (const auto& [name, type] : baseShape->propertyTypes) {
                    shape->propertyTypes[name] = type;
                }
                propertyOffset = baseShape->size;
            }
        }

        for (auto& memberPtr : classDecl->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic) {
                    auto propType = propDef->type.empty()
                        ? HIRType::makeAny()
                        : convertTypeFromString(propDef->type);
                    // Computed-name fields (`[expr] = v`) are dynamic properties, not
                    // fixed shape slots — their key is only known at runtime.
                    if (propDef->name != "[computed]") {
                        shape->propertyOffsets[propDef->name] = propertyOffset;
                        shape->propertyTypes[propDef->name] = propType;
                        propertyOffset++;
                    }
                }
            }
        }

        // Scan constructor body for this.x = expr assignments
        // ECMA-262: only the INSTANCE constructor counts; a `static constructor()`
        // is a static method that happens to be named "constructor" and has
        // unrelated semantics. Without the !isStatic filter, scanning a
        // static-constructor body crashes downstream when its `this` (the
        // class itself) is treated as an instance.
        for (auto& memberPtr : classDecl->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (method->name == "constructor" && method->hasBody && !method->isStatic) {
                    scanConstructorBodyForProperties(method->body, shape, propertyOffset);
                    break;
                }
            }
        }

        shape->size = propertyOffset;
        hirClass->shape = shape;

        // Register class shape for flat object codegen if it has properties or instance methods.
        // Classes with methods but no PropertyDefinition fields (e.g., JS classes where properties
        // are assigned in the constructor body) still need flat objects for vtable method dispatch.
        bool hasInstanceMethods_prepass = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr2.get())) {
                if (md->name != "constructor" && !md->isStatic && !md->isAbstract && md->hasBody) {
                    hasInstanceMethods_prepass = true;
                    break;
                }
            }
        }
        if (!shape->propertyOffsets.empty() || hasInstanceMethods_prepass) {
            shape->id = nextShapeId_++;
            module_->shapes.push_back(shape);
        }

        // Generate default constructor for imported classes with field initializers
        // but no explicit constructor (mirrors visitClassDeclaration behavior)
        bool hasPropertyInitializers = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr2.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Also check if there's an explicit instance constructor in the class.
        // Static methods named "constructor" don't count — they're orthogonal.
        bool hasExplicitConstructor = false;
        for (auto& memberPtr2 : classDecl->members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr2.get())) {
                if (method->name == "constructor" && method->hasBody && !method->isStatic) {
                    hasExplicitConstructor = true;
                    break;
                }
            }
        }

        if (hasPropertyInitializers && !hasExplicitConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);
            {
                // .name of the (default) constructor = the class name, or the
                // inferred binding name for an anonymous class expression.
                std::string cn = classDecl->name.empty() ? pendingClosureDisplayName_ : classDecl->name;
                if (!cn.empty()) defaultCtor->displayName = cn;
            }
            defaultCtor->params.push_back({"this", HIRType::makeClass(className, 0)});
            defaultCtor->returnType = HIRType::makeVoid();
            defaultCtor->nextValueId = 1;

            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
    // A nested function body starts OUTSIDE any enclosing try/with:
    // tryDepth_/withDepth_ are lowering-state of the OUTER function. A
    // `return` in a closure defined inside a try{} otherwise emits a
    // PopHandler for a handler the closure never pushed — at runtime it
    // popped the CALLER's exception handler (found via the Promise
    // combinator spec-path whose reject-handler vanished).
    int savedTryDepth_fn = tryDepth_; tryDepth_ = 0;
    int savedWithDepth_fn = withDepth_; withDepth_ = 0;
    bool savedWithLexical_fn = withLexical_;
    withLexical_ = withLexical_ || savedWithDepth_fn > 0;
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeClass(className, 0), "this");
            defineVariable("this", thisValue);

            // Initialize property defaults from AST. Every declared
            // instance field is installed even without initializer
            // (value defaults to undefined) per ECMA-262 15.7.
            for (auto& memberPtr2 : classDecl->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr2.get())) {
                    if (!propDef->isStatic) {
                        std::shared_ptr<HIRValue> initVal;
                        if (propDef->initializer) {
                            initVal = lowerExpression(propDef->initializer.get());
                        } else {
                            initVal = builder_.createConstUndefined();
                        }
                        emitInstanceFieldSet(thisValue, propDef, initVal);
                    }
                }
            }

            builder_.createReturnVoid();
            popScope();
            currentFunction_ = savedFunc;
    tryDepth_ = savedTryDepth_fn; withDepth_ = savedWithDepth_fn;
    withLexical_ = savedWithLexical_fn;

            hirClass->constructor = defaultCtor.get();
            module_->functions.push_back(std::move(defaultCtor));
        }

        SPDLOG_DEBUG("Created HIRClass for imported class: {} with {} properties",
            className, propertyOffset);
    }

    // Pre-register methods for imported JS slow-path classes on their HIRClass objects.
    // When a module init function compiles new ClassName(...).method(...), it needs to
    // know the method exists on the class. For typed TS classes, visitClassDeclaration
    // handles this in the first pass. For JS classes, the ClassDeclaration is inside
    // the module init body, so methods aren't registered until the init is processed.
    // We pre-register placeholder entries from the AST to enable direct VTable dispatch.
    for (auto& cls : module_->classes) {
        if (!cls->methods.empty() || cls->constructor) continue;  // Already has methods
        // Find the ClassDeclaration AST node for this class
        ast::ClassDeclaration* classDecl = nullptr;
        for (const auto& spec : specializations) {
            if (!spec.classType) continue;
            auto ct = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
            if (ct && ct->name == cls->name && ct->node) {
                classDecl = ct->node;
                break;
            }
        }
        if (!classDecl) continue;
        // Only pre-register for JS files (slow-path). TS classes are handled by
        // visitClassDeclaration in the first pass with real method registrations.
        if (classDecl->sourceFile.size() < 3 ||
            classDecl->sourceFile.substr(classDecl->sourceFile.size() - 3) != ".js") continue;
        for (auto& memberPtr : classDecl->members) {
            auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get());
            if (!md || md->name == "constructor" || md->isAbstract || !md->hasBody) continue;
            std::string methodKey = md->name;
            if (md->isGetter) methodKey = "__getter_" + md->name;
            else if (md->isSetter) methodKey = "__setter_" + md->name;
            cls->methods[methodKey] = nullptr;  // Placeholder, real ptr set later
        }
    }

    // Pre-scan: walk all non-module-init spec bodies for identifier
    // references to module-global vars, populating
    // moduleGlobalsUsedByInnerByModule_ BEFORE module_init lowering.
    // Without this, module_init lowers reads like `console.log(callCount)`
    // via the stale local-alloca fast path because the marker is only set
    // when the inner function (e.g. a class method that mutates callCount)
    // is later lowered. Result: writes from inner functions land in
    // __modvar_callCount but module_init's read pulls the stale local copy.
    {
        std::function<void(ast::Node*, const std::string&)> scanIds;
        scanIds = [&](ast::Node* node, const std::string& modPath) {
            if (!node) return;
            if (auto* id = dynamic_cast<ast::Identifier*>(node)) {
                auto it = moduleGlobalVarsByModule_.find(id->name);
                if (it != moduleGlobalVarsByModule_.end() &&
                    it->second.count(modPath)) {
                    moduleGlobalsUsedByInnerByModule_[id->name].insert(modPath);
                }
                return;
            }
            // Don't descend into nested function/method nodes — they're
            // separate specs scanned independently.
            if (dynamic_cast<ast::FunctionDeclaration*>(node)) return;
            if (dynamic_cast<ast::FunctionExpression*>(node)) return;
            if (dynamic_cast<ast::ArrowFunction*>(node)) return;
            if (auto* block = dynamic_cast<ast::BlockStatement*>(node)) {
                for (auto& s : block->statements) scanIds(s.get(), modPath);
                return;
            }
            if (auto* expr = dynamic_cast<ast::ExpressionStatement*>(node)) { scanIds(expr->expression.get(), modPath); return; }
            if (auto* ret = dynamic_cast<ast::ReturnStatement*>(node)) { scanIds(ret->expression.get(), modPath); return; }
            if (auto* ifSt = dynamic_cast<ast::IfStatement*>(node)) {
                scanIds(ifSt->condition.get(), modPath);
                scanIds(ifSt->thenStatement.get(), modPath);
                scanIds(ifSt->elseStatement.get(), modPath);
                return;
            }
            if (auto* w = dynamic_cast<ast::WhileStatement*>(node)) {
                scanIds(w->condition.get(), modPath);
                scanIds(w->body.get(), modPath);
                return;
            }
            if (auto* f = dynamic_cast<ast::ForStatement*>(node)) {
                scanIds(f->initializer.get(), modPath);
                scanIds(f->condition.get(), modPath);
                scanIds(f->incrementor.get(), modPath);
                scanIds(f->body.get(), modPath);
                return;
            }
            if (auto* fo = dynamic_cast<ast::ForOfStatement*>(node)) {
                scanIds(fo->expression.get(), modPath);
                scanIds(fo->body.get(), modPath);
                return;
            }
            if (auto* fi = dynamic_cast<ast::ForInStatement*>(node)) {
                scanIds(fi->expression.get(), modPath);
                scanIds(fi->body.get(), modPath);
                return;
            }
            if (auto* sw = dynamic_cast<ast::SwitchStatement*>(node)) {
                scanIds(sw->expression.get(), modPath);
                for (auto& cl : sw->clauses) {
                    if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) {
                        scanIds(cc->expression.get(), modPath);
                        for (auto& s : cc->statements) scanIds(s.get(), modPath);
                    }
                    if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) {
                        for (auto& s : dc->statements) scanIds(s.get(), modPath);
                    }
                }
                return;
            }
            if (auto* tryStmt = dynamic_cast<ast::TryStatement*>(node)) {
                for (auto& s : tryStmt->tryBlock) scanIds(s.get(), modPath);
                if (tryStmt->catchClause) {
                    for (auto& s : tryStmt->catchClause->block) scanIds(s.get(), modPath);
                }
                for (auto& s : tryStmt->finallyBlock) scanIds(s.get(), modPath);
                return;
            }
            if (auto* th = dynamic_cast<ast::ThrowStatement*>(node)) { scanIds(th->expression.get(), modPath); return; }
            if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(node)) {
                scanIds(vd->initializer.get(), modPath);
                return;
            }
            if (auto* lab = dynamic_cast<ast::LabeledStatement*>(node)) { scanIds(lab->statement.get(), modPath); return; }
            if (auto* call = dynamic_cast<ast::CallExpression*>(node)) {
                scanIds(call->callee.get(), modPath);
                for (auto& a : call->arguments) scanIds(a.get(), modPath);
                return;
            }
            if (auto* ne = dynamic_cast<ast::NewExpression*>(node)) {
                scanIds(ne->expression.get(), modPath);
                for (auto& a : ne->arguments) scanIds(a.get(), modPath);
                return;
            }
            if (auto* bin = dynamic_cast<ast::BinaryExpression*>(node)) {
                scanIds(bin->left.get(), modPath);
                scanIds(bin->right.get(), modPath);
                return;
            }
            if (auto* as = dynamic_cast<ast::AssignmentExpression*>(node)) {
                scanIds(as->left.get(), modPath);
                scanIds(as->right.get(), modPath);
                return;
            }
            if (auto* c = dynamic_cast<ast::ConditionalExpression*>(node)) {
                scanIds(c->condition.get(), modPath);
                scanIds(c->whenTrue.get(), modPath);
                scanIds(c->whenFalse.get(), modPath);
                return;
            }
            if (auto* p = dynamic_cast<ast::PrefixUnaryExpression*>(node)) { scanIds(p->operand.get(), modPath); return; }
            if (auto* p2 = dynamic_cast<ast::PostfixUnaryExpression*>(node)) { scanIds(p2->operand.get(), modPath); return; }
            if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(node)) { scanIds(pa->expression.get(), modPath); return; }
            if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(node)) {
                scanIds(ea->expression.get(), modPath);
                scanIds(ea->argumentExpression.get(), modPath);
                return;
            }
            if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(node)) {
                for (auto& e : arr->elements) scanIds(e.get(), modPath);
                return;
            }
            if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(node)) {
                for (auto& pr : obj->properties) {
                    if (auto* pa2 = dynamic_cast<ast::PropertyAssignment*>(pr.get())) {
                        scanIds(pa2->initializer.get(), modPath);
                    }
                }
                return;
            }
            if (auto* tmpl = dynamic_cast<ast::TemplateExpression*>(node)) {
                for (auto& span : tmpl->spans) scanIds(span.expression.get(), modPath);
                return;
            }
            if (auto* paren = dynamic_cast<ast::ParenthesizedExpression*>(node)) { scanIds(paren->expression.get(), modPath); return; }
            if (auto* sp = dynamic_cast<ast::SpreadElement*>(node)) { scanIds(sp->expression.get(), modPath); return; }
            if (auto* del = dynamic_cast<ast::DeleteExpression*>(node)) { scanIds(del->expression.get(), modPath); return; }
            if (auto* aw = dynamic_cast<ast::AwaitExpression*>(node)) { scanIds(aw->expression.get(), modPath); return; }
            if (auto* y = dynamic_cast<ast::YieldExpression*>(node)) { scanIds(y->expression.get(), modPath); return; }
            if (auto* asx = dynamic_cast<ast::AsExpression*>(node)) { scanIds(asx->expression.get(), modPath); return; }
            if (auto* nn = dynamic_cast<ast::NonNullExpression*>(node)) { scanIds(nn->expression.get(), modPath); return; }
        };
        for (const auto& spec : specializations) {
            if (spec.originalName.find("__module_init_") == 0) continue;
            if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                for (auto& s : funcNode->body) scanIds(s.get(), spec.modulePath);
            } else if (auto* methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
                for (auto& s : methodNode->body) scanIds(s.get(), spec.modulePath);
            }
        }

        // Class-expression methods aren't in `specializations` (the
        // Monomorphizer doesn't synthesize specs for anonymous class
        // members). Walk the AST directly so `var X = 0; var C = class
        // { m(){ X = X + 1; } }; new C().m(); console.log(X)` registers
        // the marker in time for module_init's `console.log(X)` read to
        // take the LoadGlobal path. Without this, the read pulls a stale
        // local copy and assertions like
        // `assert.sameValue(callCount, 1, 'method invoked exactly once')`
        // see 0 instead of the incremented value.
        for (auto& stmt : program->body) {
            auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
            if (!varDecl || !varDecl->initializer) continue;
            auto* classExpr = dynamic_cast<ast::ClassExpression*>(varDecl->initializer.get());
            if (!classExpr) continue;
            for (auto& memberPtr : classExpr->members) {
                auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get());
                if (!md || !md->hasBody) continue;
                for (auto& s : md->body) scanIds(s.get(), currentModulePath_);
            }
        }
    }

    // Second pass: generate functions from specializations
    SPDLOG_WARN("[ASTToHIR] Generating {} specializations...", specializations.size());
    size_t specIdx = 0;
    for (const auto& spec : specializations) {
        specIdx++;
        if (specIdx % 20 == 0) {
            SPDLOG_WARN("[ASTToHIR] spec {}/{}: {}", specIdx, specializations.size(), spec.specializedName);
        }
        if (spec.specializedName.find("lambda") != std::string::npos) {
            // Skip lambda specializations - they'll be generated when encountered
            continue;
        }

        // Track current module path for cross-module function name disambiguation
        currentModulePath_ = spec.modulePath;

        // Get the node - could be FunctionDeclaration or MethodDefinition
        if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
            // Create HIR function with the specialized name
            auto func = std::make_unique<HIRFunction>(spec.specializedName);
            func->isAsync = funcNode->isAsync;
            func->isGenerator = funcNode->isGenerator;
            func->sourceLine = funcNode->line;
            func->sourceFile = funcNode->sourceFile;
            func->displayName = funcNode->name;

            // Collect destructured parameter patterns for later extraction
            struct SpecDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<SpecDestructuredParam> specDestructuredParams;

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

                // If parameter has a default value, it must be Any type to receive undefined
                if (param->initializer) {
                    paramType = HIRType::makeAny();
                }

                // Get parameter name
                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    specDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    specDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                if (param->isRest) {
                    func->hasRestParam = true;
                }

                // ECMA-262 §10.2.5: function .length is the count of leading
                // simple parameters (no default, no rest, no destructuring
                // pattern). Record the first non-simple index so emit-time
                // arity uses the correct value. SIZE_MAX (initial value)
                // means all params are simple.
                if (func->firstNonSimpleParamIndex == SIZE_MAX) {
                    bool isDestructured =
                        dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                        dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
                    if (param->initializer || param->isRest || isDestructured) {
                        func->firstNonSimpleParamIndex = paramIdx;
                    }
                }

                func->params.push_back({paramName, paramType});
            }

            // If the function body uses 'arguments', add hidden __argN__ params
            // so extra call arguments beyond declared params can be captured.
            {
                bool bodyUsesArguments = false;
                for (auto& stmt : funcNode->body) {
                    if (containsArgumentsIdentifier(stmt.get())) {
                        bodyUsesArguments = true;
                        break;
                    }
                }
                if (bodyUsesArguments) {
                    while (func->params.size() < 10) {
                        std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                        func->params.push_back({argName, HIRType::makeAny()});
                    }
                }
            }

            // Set return type from specialization
            if (spec.returnType) {
                func->returnType = convertType(spec.returnType);
            } else if (!funcNode->returnType.empty()) {
                func->returnType = convertTypeFromString(funcNode->returnType);
            } else {
                func->returnType = HIRType::makeAny();
            }

            // Push function to module BEFORE lowering body so recursive calls
            // can find this function (e.g., fib calling fib). Without this,
            // the recursive call's return type defaults to Any, causing
            // ts_value_add instead of native fadd for arithmetic.
            auto* funcPtr = func.get();
            module_->functions.push_back(std::move(func));

            // Create entry block and set up for lowering
            auto entryBlock = funcPtr->createBlock("entry");
            currentFunction_ = funcPtr;
            currentBlock_ = entryBlock;
            builder_.setInsertPoint(entryBlock);

            // Push function scope and bind parameters
            pushFunctionScope(funcPtr);
            funcPtr->nextValueId = static_cast<uint32_t>(funcPtr->params.size());
            for (size_t i = 0; i < funcPtr->params.size(); ++i) {
                const auto& [paramName, paramType] = funcPtr->params[i];
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
                    auto defaultBB = funcPtr->createBlock("default_param");
                    auto usedBB = funcPtr->createBlock("use_param");
                    auto mergeBB = funcPtr->createBlock("param_merge");

                    // Branch based on undefined check
                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    // Default block - evaluate default expression and store
                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
                    // Force box the default value if parameter type is Any
                    // We use forceBoxValue because the expression might be a function call
                    // that gets inlined later, changing its type from Any to a concrete type
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
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
                    // No default value - store into an alloca so reassignment works
                    // (LLVM's mem2reg will eliminate the alloca for params that are never reassigned)
                    auto allocaVal = builder_.createAlloca(paramType);
                    builder_.createStore(paramValue, allocaVal);
                    defineVariableAlloca(paramName, allocaVal, paramType);
                }
            }

            // Emit destructuring extraction for parameters with binding patterns
            for (auto& dp : specDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    funcPtr->params[dp.paramIndex].first);
                // Apply parameter default value if the AST recorded one for this
                // pattern parameter (e.g. `function f([a] = [1])`). Without this
                // step, the destructure source would be the raw `undefined` arg
                // and downstream RequireObjectCoercible / GetIterator would
                // throw incorrectly.
                if (dp.defaultInitializer) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer);
                    if (defaultExpr) {
                        auto defaultVal = lowerExpression(defaultExpr);
                        defaultVal = boxValueIfNeeded(defaultVal);
                        paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                    }
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // JavaScript function hoisting: pre-declare nested function names as variables
            // This allows functions to be called before they appear in source order.
            // We create allocas for function names, which will be filled when the function
            // declaration is processed. Calls to these names will use indirect call.
            for (auto& stmt : funcNode->body) {
                if (auto* nestedFunc = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    // Create a function type for the closure
                    auto nestedFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
                    for (auto& param : nestedFunc->parameters) {
                        std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                        if (!param->type.empty()) {
                            paramType = convertTypeFromString(param->type);
                        }
                        nestedFuncType->paramTypes.push_back(paramType);
                    }
                    nestedFuncType->returnType = nestedFunc->returnType.empty()
                        ? HIRType::makeAny()
                        : convertTypeFromString(nestedFunc->returnType);

                    // Create an alloca for the function variable (will hold closure or function ptr)
                    auto allocaVal = builder_.createAlloca(nestedFuncType, nestedFunc->name);
                    // Initialize with null - will be set when the function is processed
                    builder_.createStore(builder_.createConstNull(), allocaVal);
                    defineVariableAlloca(nestedFunc->name, allocaVal, nestedFuncType);
                }
            }

            // ECMA-262 §14.3.2: hoist every `var` declaration in the
            // function body — including those nested inside if/else,
            // loops, try, switch — to the FunctionEnvironment. Without
            // this, an assignment in branch B to a `var` declared in
            // branch A binds to a fresh slot per branch (or the global
            // object) and the surrounding function sees `undefined`.
            // Also catches nested FunctionDeclarations so closures see
            // them in source-order-independent ways.
            {
                std::vector<std::string> hoistedVars;
                for (auto& stmt : funcNode->body) {
                    collectHoistedVarNames(stmt.get(), hoistedVars);
                }
                for (auto& name : hoistedVars) {
                    if (lookupVariableInfoInCurrentFunction(name)) continue;
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
                    builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
                    defineVariableAlloca(name, allocaVal, HIRType::makeAny());
                }
            }

            // Pre-declare top-level let/const so nested FunctionDeclaration
            // bodies (lowered in pass 1 below) can resolve outer-scope
            // captures. Mirrors the same block in visitFunctionDeclaration.
            for (auto& stmt : funcNode->body) {
                if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                    if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
                    auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
                    if (!ident) continue;
                    if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                    // TDZ sentinel: read-before-declaration throws (ts_tdz_check).
                    auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
                    builder_.createStore(tdz, allocaVal, HIRType::makeAny());
                    defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                    if (auto* vi = lookupVariableInfoInCurrentFunction(ident->name)) vi->isTDZ = true;
                }
            }

            // Create 'arguments' array-like object if the function body references 'arguments'.
            // Must be done at function entry (before any other code) because inner calls
            // will overwrite ts_last_call_argc, making lazy creation incorrect.
            {
                bool usesArguments = false;
                for (auto& stmt : funcNode->body) {
                    if (containsArgumentsIdentifier(stmt.get())) {
                        usesArguments = true;
                        break;
                    }
                }
                if (usesArguments) {
                    std::vector<std::shared_ptr<HIRValue>> callArgs;
                    size_t userIdx = 0;
                    for (size_t i = 0; i < funcPtr->params.size() && userIdx < 10; ++i) {
                        if (funcPtr->params[i].first == "__closure__") continue;
                        auto paramVal = lookupVariable(funcPtr->params[i].first);
                        if (!paramVal) {
                            paramVal = builder_.createConstUndefined();
                        }
                        callArgs.push_back(paramVal);
                        userIdx++;
                    }
                    while (userIdx < 10) {
                        callArgs.push_back(builder_.createConstUndefined());
                        userIdx++;
                    }

                    auto argsArray = builder_.createCall("ts_create_arguments_from_params",
                        callArgs, HIRType::makeAny());
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
                    builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
                    defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
                }
            }

            // If this is the entry-point function (user_main or its
            // synthetic equivalent for top-level scripts), flush deferred
            // class-prototype installs and static-property initializers
            // at the very start of the body so that subsequent statements
            // see `E.prototype.<key>` populated. This is the path most
            // top-level code goes through (the visitor-based
            // visitFunctionDeclaration is not used for spec functions).
            if (funcNode->name == "user_main" || funcNode->name == "__synthetic_user_main") {
                emitDeferredStaticInits();
            }

            // Lower function body in two passes for proper JavaScript function hoisting:
            // FIRST PASS: Process FunctionDeclarations to create closures
            // This ensures nested functions are available before any other code runs.
            for (auto& stmt : funcNode->body) {
                if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    lowerStatement(stmt.get());
                }
            }
            emitMutualRecursionFixup();

            // Script-goal global bindings (ES 9.1.1.4.17/18): top-level
            // function declarations must be reflected as OWN properties of
            // globalThis — Object.hasOwn(globalThis, "$DONE") gates the
            // test262 asyncHelpers harness. Emit after pass-1 hoisting so
            // the closure variables exist; identifier reads still resolve
            // through the static binding (the reflection is additive).
            // Top-level statements live in the __module_init_* synthetic
            // functions (JS modules) or the entry function (TS scripts).
            if (funcNode->name == "user_main" ||
                funcNode->name == "__synthetic_user_main" ||
                funcNode->name.rfind("__module_init", 0) == 0) {
                for (auto& stmt : funcNode->body) {
                    auto* fd = dynamic_cast<ast::FunctionDeclaration*>(stmt.get());
                    if (!fd || fd->name.empty()) continue;
                    auto fnVal = lookupVariable(fd->name);
                    if (!fnVal) continue;
                    auto nameStr = builder_.createConstString(fd->name);
                    builder_.createCall("ts_global_bind_fn", {nameStr, fnVal},
                                        HIRType::makeVoid());
                }
            }

            // SECOND PASS: Process non-FunctionDeclaration statements in order
            SPDLOG_DEBUG("[SPEC] Body pass 2: {} stmts in {}", funcNode->body.size(), spec.specializedName);
            for (auto& stmt : funcNode->body) {
                if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    SPDLOG_DEBUG("[SPEC]   stmt kind={}", stmt ? stmt->getKind() : "null");
                    lowerStatement(stmt.get());
                    if (builder_.isBlockTerminated()) {
                        break;
                    }
                }
            }

            // Add implicit return if needed
            if (!hasTerminator()) {
                if (funcPtr->returnType->kind == HIRTypeKind::Void) {
                    builder_.createReturnVoid();
                } else {
                    // Return undefined for non-void functions without explicit return
                    auto undef = builder_.createConstUndefined();
                    builder_.createReturn(undef);
                }
            }

            popScope();
            // func already pushed to module_->functions before body lowering
        } else if (auto* methodNode = dynamic_cast<ast::MethodDefinition*>(spec.node)) {
            // Handle method definitions (similar to above)
            if (methodNode->isAbstract || !methodNode->hasBody) continue;

            auto func = std::make_unique<HIRFunction>(spec.specializedName);
            func->isAsync = methodNode->isAsync;
            func->isGenerator = methodNode->isGenerator;
            func->sourceLine = methodNode->line;
            func->sourceFile = methodNode->sourceFile;
            func->displayName = methodNode->name;

            // Add 'this' parameter first for instance methods
            // spec.argTypes[0] is the class type for 'this' (set by Monomorphizer)
            size_t argTypeOffset = 0;
            if (!methodNode->isStatic) {
                auto thisType = (spec.argTypes.size() > 0 && spec.argTypes[0])
                    ? convertType(spec.argTypes[0])
                    : HIRType::makeAny();
                func->params.push_back({"this", thisType});
                argTypeOffset = 1;  // Skip 'this' in spec.argTypes for regular params
            }

            // Collect destructured parameter patterns so we can emit the
            // extraction (get_elem / get_prop) code at method entry — same
            // shape as the FunctionDeclaration path above. Without this,
            // `class C { method([x, y, z]) {} }` produces a HIR function
            // with a single `param0` and no destructuring, leaving x/y/z
            // unbound and crashing on use.
            struct MethDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<MethDestructuredParam> methDestructuredParams;

            // Handle regular parameters
            for (size_t paramIdx = 0; paramIdx < methodNode->parameters.size(); ++paramIdx) {
                auto& param = methodNode->parameters[paramIdx];
                std::shared_ptr<HIRType> paramType;
                size_t specIdx = paramIdx + argTypeOffset;
                if (specIdx < spec.argTypes.size() && spec.argTypes[specIdx]) {
                    paramType = convertType(spec.argTypes[specIdx]);
                } else if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                } else {
                    paramType = HIRType::makeAny();
                }

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    methDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    methDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }

                // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
                if (func->firstNonSimpleParamIndex == SIZE_MAX) {
                    bool isDestructured =
                        dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                        dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
                    if (param->initializer || param->isRest || isDestructured) {
                        func->firstNonSimpleParamIndex = paramIdx;
                    }
                }

                func->params.push_back({paramName, paramType});
            }

            // If the method body uses `arguments`, pad with hidden __argN__
            // params so extra call args physically reach
            // ts_create_arguments_from_params (class-DECL static methods
            // compile through this spec branch and read arguments[N] as
            // undefined without the pad; length was already right via
            // ts_last_call_argc).
            {
                bool mBodyUsesArgs = false;
                for (auto& stmt : methodNode->body) {
                    if (containsArgumentsIdentifier(stmt.get())) { mBodyUsesArgs = true; break; }
                }
                if (mBodyUsesArgs) {
                    while (func->params.size() < 10) {
                        std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                        func->params.push_back({argName, HIRType::makeAny()});
                    }
                }
            }

            // Set return type
            if (spec.returnType) {
                func->returnType = convertType(spec.returnType);
            } else if (!methodNode->returnType.empty()) {
                func->returnType = convertTypeFromString(methodNode->returnType);
            } else {
                func->returnType = HIRType::makeAny();
            }

            // Push method to module BEFORE lowering body (same reason as functions above).
            // visitClassDeclaration emits a same-named placeholder during the
            // early pre-pass — but at pre-pass time the module-scope vars
            // haven't been registered, so identifier writes there miss the
            // StoreGlobal path and the body becomes a no-op. Replace any
            // existing entry so HIRToLLVM lowers ONE HIRFunction (and
            // InliningPass picks the spec-loop body, which IS scope-correct).
            // Without this, both HIRFunctions push and HIRToLLVM merges them
            // into one llvm::Function with two entry blocks; the spec-loop's
            // body becomes "entry1: No predecessors!" — unreachable.
            auto* methPtr = func.get();
            bool replacedExisting = false;
            for (auto& existing : module_->functions) {
                if (existing && existing->name == spec.specializedName) {
                    // HIRClass fields (vtable, methods, staticMethods, constructor)
                    // hold raw HIRFunction* into module_->functions. Replacing the
                    // unique_ptr here destroys the old HIRFunction; any HIRClass
                    // pointer to it becomes dangling and HIRToLLVM later reads
                    // freed memory via methodFunc->mangledName. Retarget them
                    // to the new HIRFunction before the move.
                    HIRFunction* oldPtr = existing.get();
                    for (auto& cls : module_->classes) {
                        if (!cls) continue;
                        for (auto& entry : cls->vtable) {
                            if (entry.second == oldPtr) entry.second = methPtr;
                        }
                        for (auto& entry : cls->methods) {
                            if (entry.second == oldPtr) entry.second = methPtr;
                        }
                        for (auto& entry : cls->staticMethods) {
                            if (entry.second == oldPtr) entry.second = methPtr;
                        }
                        if (cls->constructor == oldPtr) cls->constructor = methPtr;
                    }
                    existing = std::move(func);
                    replacedExisting = true;
                    break;
                }
            }
            if (!replacedExisting) {
                module_->functions.push_back(std::move(func));
            }

            auto entryBlock = methPtr->createBlock("entry");
            currentFunction_ = methPtr;
            currentBlock_ = entryBlock;
            builder_.setInsertPoint(entryBlock);

            // Set currentClass_ so that property type resolution works for 'this' access
            // (e.g., this.items resolves to array<string> instead of any)
            HIRClass* savedClass = currentClass_;
            if (spec.classType) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (classType) {
                    for (auto& cls : module_->classes) {
                        if (cls->name == classType->name) {
                            currentClass_ = cls.get();
                            break;
                        }
                    }
                }
            }

            pushFunctionScope(methPtr);
            methPtr->nextValueId = static_cast<uint32_t>(methPtr->params.size());
            // argTypeOffset is 1 for instance methods (slot 0 = synthetic 'this',
            // user params start at HIR index 1) and 0 for static methods. Map
            // the HIR param index back to the AST parameter so we can honor
            // default-value initializers (e.g. `method(a = 99) {}`). Without
            // this mapping the spec path silently drops scalar defaults — the
            // earlier destructured-default fix (loop below) only handled
            // `method([a] = [1])` patterns.
            for (size_t i = 0; i < methPtr->params.size(); ++i) {
                const auto& [paramName, paramType] = methPtr->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                // Map HIR param index → AST parameter index. Skip the
                // synthetic 'this' (argTypeOffset==1, i==0) which has no AST
                // counterpart. Skip destructured params — they are handled by
                // the methDestructuredParams loop below which applies defaults
                // before pattern extraction.
                size_t astParamIdx = (i >= argTypeOffset) ? (i - argTypeOffset) : SIZE_MAX;
                ast::Parameter* astParam = (astParamIdx < methodNode->parameters.size())
                    ? methodNode->parameters[astParamIdx].get() : nullptr;
                bool isDestructured = astParam && (
                    dynamic_cast<ast::ObjectBindingPattern*>(astParam->name.get()) ||
                    dynamic_cast<ast::ArrayBindingPattern*>(astParam->name.get()));

                if (astParam && astParam->initializer && !isDestructured) {
                    // Scalar default (e.g. `method(a = 99) {}`). Mirror the
                    // FunctionDeclaration spec path: branch on undefined, use
                    // the default expression, otherwise use the passed value.
                    auto allocaVal = builder_.createAlloca(paramType);
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    auto defaultBB = methPtr->createBlock("default_param");
                    auto usedBB = methPtr->createBlock("use_param");
                    auto mergeBB = methPtr->createBlock("param_merge");

                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr)
                                               : builder_.createConstUndefined();
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    auto allocaVal = builder_.createAlloca(paramType);
                    builder_.createStore(paramValue, allocaVal);
                    defineVariableAlloca(paramName, allocaVal, paramType);
                }
            }

            // Emit destructuring extraction for parameters with binding
            // patterns. Mirrors the FunctionDeclaration path above; without
            // this, class `method([x, y, z]) {}` would receive `param0` but
            // never bind x/y/z, crashing on use.
            for (auto& dp : methDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    methPtr->params[dp.paramIndex].first);
                // Apply parameter default value if recorded for this pattern
                // parameter (e.g. `method([a] = [1])`).
                if (dp.defaultInitializer) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer);
                    if (defaultExpr) {
                        auto defaultVal = lowerExpression(defaultExpr);
                        defaultVal = boxValueIfNeeded(defaultVal);
                        paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                    }
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // Create the 'arguments' object if the method body references it.
            // Class methods lowered via this monomorphized path previously lacked
            // it, so `arguments` was undefined. Mirrors the object-literal method
            // site; in the eager param prologue so generator/async methods
            // capture call-time args. The physical `this` (slot 0 for instance
            // methods) and `__closure__` are EXCLUDED — `arguments` holds JS
            // args only, and ts_last_call_argc is already the JS argc (set by
            // ts_call_with_this_N, receiver excluded). Always emit exactly 10
            // operands: ts_create_arguments_from_params is declared lazily with
            // arity fixed by the first call site, so a differing count fails LLVM
            // verification ("Incorrect number of arguments passed").
            {
                bool usesArguments = false;
                for (auto& stmt : methodNode->body) {
                    if (containsArgumentsIdentifier(stmt.get())) { usesArguments = true; break; }
                }
                if (usesArguments) {
                    std::vector<std::shared_ptr<HIRValue>> callArgs;
                    size_t userIdx = 0;
                    for (size_t i = 0; i < methPtr->params.size() && userIdx < 10; ++i) {
                        if (methPtr->params[i].first == "__closure__" ||
                            methPtr->params[i].first == "this") continue;
                        auto paramVal = lookupVariable(methPtr->params[i].first);
                        if (!paramVal) paramVal = builder_.createConstUndefined();
                        callArgs.push_back(paramVal);
                        userIdx++;
                    }
                    while (userIdx < 10) {
                        callArgs.push_back(builder_.createConstUndefined());
                        userIdx++;
                    }
                    auto argsArray = builder_.createCall(
                        "ts_create_arguments_from_params", callArgs, HIRType::makeAny());
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
                    builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
                    defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
                }
            }

            // Async generators: end of PARAMETER prologue — body throws after
            // this reject the first next() promise (ts_agen_should_reject).
            // Mirrors the FunctionDeclaration/arrow/funcExpr/method sites.
            if (methPtr->isAsync && methPtr->isGenerator) {
                builder_.createCall("ts_async_generator_body_started", {},
                                    HIRType::makeVoid());
            } else if (methPtr->isGenerator) {
                // Sync generator method: eager-parameter model (see the
                // FunctionDeclaration site). Marker = suspension 0 -> 1.
                builder_.createCall("ts_generator_body_started", {},
                                    HIRType::makeVoid());
            }

            // For constructors of imported classes, emit field initializers
            // before the constructor body (mirrors visitClassDeclaration behavior)
            if (methodNode->name == "constructor" && spec.classType) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (classType && classType->node) {
                    auto thisValue = lookupVariable("this");
                    if (thisValue) {
                        for (auto& member : classType->node->members) {
                            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                                if (!propDef->isStatic) {
                                    // ECMA-262 15.7: install every
                                    // instance field; default to
                                    // undefined when no initializer.
                                    std::shared_ptr<HIRValue> initVal;
                                    if (propDef->initializer) {
                                        initVal = lowerExpression(propDef->initializer.get());
                                    } else {
                                        initVal = builder_.createConstUndefined();
                                    }
                                    emitInstanceFieldSet(thisValue, propDef, initVal);
                                }
                            }
                        }
                    }
                }
            }

            // Pre-declare hoisted `var` names so a nested function declaration
            // (lowered in the first pass below, BEFORE the var initializers run
            // in the second pass) can capture the method's locals. Without this
            // the hoisted nested function is lowered when the slot doesn't exist
            // yet, so it captures nothing (make_closure with no captures) and
            // reads undefined — the `var self = this; function inner(){ self.#x }`
            // idiom then crashes. Mirrors the spec-function body lowering.
            {
                std::vector<std::string> methodHoistedVars;
                for (auto& stmt : methodNode->body)
                    collectHoistedVarNames(stmt.get(), methodHoistedVars);
                for (auto& name : methodHoistedVars) {
                    if (lookupVariableInfoInCurrentFunction(name)) continue;
                    auto a = builder_.createAlloca(HIRType::makeAny(), name);
                    builder_.createStore(builder_.createConstUndefined(), a, HIRType::makeAny());
                    defineVariableAlloca(name, a, HIRType::makeAny());
                }
                // Top-level let/const: TDZ sentinel pre-declaration so nested
                // function declarations capture them (mirrors the function
                // paths; `let self = this; function inner(){ self.#m = v; }`
                // in a class-expression method read `self` as undefined).
                for (auto& stmt : methodNode->body) {
                    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                        if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
                        auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
                        if (!ident) continue;
                        if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
                        auto a = builder_.createAlloca(HIRType::makeAny(), ident->name);
                        auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
                        builder_.createStore(tdz, a, HIRType::makeAny());
                        defineVariableAlloca(ident->name, a, HIRType::makeAny());
                        if (auto* vi = lookupVariableInfoInCurrentFunction(ident->name)) vi->isTDZ = true;
                    }
                }
            }

            // Two-pass for function hoisting: FIRST process FunctionDeclarations
            for (auto& stmt : methodNode->body) {
                if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    lowerStatement(stmt.get());
                }
            }
            // SECOND pass: process non-FunctionDeclaration statements
            for (auto& stmt : methodNode->body) {
                if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                    lowerStatement(stmt.get());
                    if (builder_.isBlockTerminated()) {
                        break;
                    }
                }
            }

            if (!hasTerminator()) {
                if (methPtr->returnType->kind == HIRTypeKind::Void) {
                    builder_.createReturnVoid();
                } else {
                    auto undef = builder_.createConstUndefined();
                    builder_.createReturn(undef);
                }
            }

            popScope();
            currentClass_ = savedClass;  // Restore after method body lowering

            // Link method to its HIRClass (for constructor calls and method resolution)
            if (spec.classType) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (classType) {
                    std::string className = classType->name;
                    HIRClass* hirClass = nullptr;
                    for (auto& cls : module_->classes) {
                        if (cls->name == className) {
                            hirClass = cls.get();
                            break;
                        }
                    }
                    if (hirClass) {
                        if (methodNode->name == "constructor") {
                            hirClass->constructor = methPtr;
                        } else if (methodNode->isStatic) {
                            hirClass->staticMethods[methodNode->name] = methPtr;
                        } else {
                            std::string methodKey = methodNode->name;
                            if (methodNode->isGetter) methodKey = "__getter_" + methodNode->name;
                            else if (methodNode->isSetter) methodKey = "__setter_" + methodNode->name;
                            hirClass->methods[methodKey] = methPtr;
                            // visitClassDeclaration may have already pushed a
                            // vtable entry for this method (with a stale func
                            // pointer if we replaced the HIRFunction in
                            // module_->functions). Replace the existing entry
                            // by methodKey rather than appending — otherwise
                            // VTable codegen sees two entries and the first
                            // points to a freed unique_ptr.
                            bool updatedVtable = false;
                            for (auto& vt : hirClass->vtable) {
                                if (vt.first == methodKey) {
                                    vt.second = methPtr;
                                    updatedVtable = true;
                                    break;
                                }
                            }
                            if (!updatedVtable) {
                                hirClass->vtable.push_back({methodKey, methPtr});
                            }
                        }
                    }
                }
            }

            // func already pushed to module_ before body lowering
        }
    }

    popScope();
    specializations_ = nullptr;  // Clear to avoid dangling pointer
    return std::move(module_);
}

//==============================================================================
// SSA Helpers
//==============================================================================

void ASTToHIR::emitComputedAccessorInstalls(HIRClass* hirClass,
                                            std::shared_ptr<HIRValue> proto,
                                            std::shared_ptr<HIRValue> ctorVal) {
    if (!hirClass) return;
    for (auto& ca : hirClass->computedAccessors) {
        // Static computed FIELD (`static [x] = init`): install the evaluated init
        // under the evaluated key on the constructor, here at the source position
        // (where the key's variable is bound).
        if (ca.isField) {
            if (!ca.keyExpr || !ca.initExpr || !ctorVal) continue;
            auto keyVal = lowerExpression(static_cast<ast::Expression*>(ca.keyExpr));
            auto initVal = lowerExpression(static_cast<ast::Expression*>(ca.initExpr));
            builder_.createSetPropDynamic(ctorVal, keyVal, initVal);
            continue;
        }
        if (!ca.func || !ca.keyExpr) continue;
        auto keyVal = lowerExpression(static_cast<ast::Expression*>(ca.keyExpr));
        // Redirect to the Monomorphizer-emitted "<Class>_set_[computed]" /
        // "_get_[computed]" / "_[computed]" copy when ca.func is unusable: either an
        // EMPTY accessor placeholder (instrCount<=1) OR a body lowered at MODULE level
        // (class DECLARATION: currentFunc==null), where module-scope variables aren't
        // bound so a free var like `g` in `[k]() { return g; }` resolved to a constant
        // `undefined` (inttoptr i64 10) instead of `load @__modvar_g`. For a class
        // EXPRESSION the body is lowered in a real function context (correct) and the
        // monomorphized "[computed]" copy may not exist — keep ca.func there.
        std::string fnName = ca.func->name;
        if (hirClass) {
            size_t instrCount = 0;
            for (auto& b : ca.func->blocks) instrCount += b->instructions.size();
            if (instrCount <= 1 || ca.moduleLevelBody) {
                std::string pfx = ca.isMethod ? "" : (ca.isSetter ? "set_" : "get_");
                std::string smark = ca.isStatic ? "static_" : "";
                fnName = hirClass->name + "_" + smark + pfx + "[computed]";
            }
        }
        auto closure = builder_.createLoadFunction(fnName);
        auto recv = ca.isStatic ? ctorVal : proto;
        if (!recv) continue;
        const char* installFn = ca.isMethod
            ? "ts_class_install_computed_method"
            : ca.isSetter ? "ts_class_install_computed_setter"
                          : "ts_class_install_computed_getter";
        builder_.createCall(installFn, {recv, keyVal, closure}, HIRType::makeVoid());
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
        // Mirror the initialized value onto the constructor closure as an own
        // property, so static fields are reachable through a non-literal-name
        // reference (alias / dynamic key / passed). The typed global above
        // remains authoritative for the literal-name direct-read fast path.
        if (!init.ctorName.empty() && !init.fieldName.empty()) {
            auto ctorVal = builder_.createLoadFunction(init.ctorName);
            // A COMPUTED static field name (`static [expr] = v`): evaluate the
            // key and install dynamically — the "[computed]" placeholder is not
            // a usable property name.
            if (init.computedNameNode) {
                if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(init.computedNameNode)) {
                    auto key = lowerExpression(cpn->expression.get());
                    builder_.createSetPropDynamic(ctorVal, key, initVal);
                } else {
                    builder_.createSetPropStatic(ctorVal, privateStorageKey(init.fieldName), initVal);
                }
            } else {
                builder_.createSetPropStatic(ctorVal, privateStorageKey(init.fieldName), initVal);
            }
        }
    }
    deferredStaticInits_.clear();  // Only emit once

    // Install class prototypes. For each class with instance methods or
    // accessors, build a real prototype object holding `__getter_<key>`,
    // `__setter_<key>`, and method names, then assign it to the
    // constructor's `prototype` property. This makes
    // `E.prototype['<key>']` find the installed function and supports
    // test262 accessor-name probes that read directly from the
    // prototype.
    // Helper to install a class method/accessor with the spec method
    // descriptor: { writable: true, enumerable: false, configurable: true }.
    // verifyProperty(C.prototype, "m", { enumerable: false, ... }) and
    // Object.keys(C) tests require non-enumerable. Routes through the
    // ts_object_set_method runtime which uses TsMap::SetWithAttrs.
    auto installMethod = [&](std::shared_ptr<HIRValue> recv,
                             const std::string& key,
                             std::shared_ptr<HIRValue> closure) {
        installClassMember(recv, key, closure);  // shared with the class-expr trailer
    };
    for (auto* hirClass : deferredClassPrototypes_) {
        if (!hirClass) continue;
        // Every class needs a real prototype object (not just classes with
        // user-defined methods) so that `c.constructor === C` and
        // `Object.getPrototypeOf(c) === C.prototype` hold per ECMA-262
        // §15.7. Previously classes with only staticMethods (or no
        // methods at all) skipped the prototype install entirely, leaving
        // a default Function.prototype object with no constructor backref.
        std::string ctorName = hirClass->constructor
            ? hirClass->constructor->name
            : hirClass->name + "_constructor";
        auto ctorVal = builder_.createLoadFunction(ctorName);

        // Build the prototype map. Always create one — even for classes
        // without methods — so the constructor backref and parent-prototype
        // chain can be installed.
        auto proto = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());
        for (auto& [methodKey, methodFunc] : hirClass->methods) {
            if (!methodFunc) continue;
            auto methodClosure = builder_.createLoadFunction(completeMethodSymbol(hirClass, methodKey, methodFunc));
            installMethod(proto, methodKey, methodClosure);
        }

        // ECMA-262 §15.7: `Class.prototype.constructor` is the class
        // itself, with {writable:true, enumerable:false, configurable:true}.
        // installMethod uses ts_object_set_method which writes with those
        // exact descriptor flags.
        installMethod(proto, "constructor", ctorVal);

        // ECMA-262 §15.7.14 (ClassDefinitionEvaluation): if the class
        // extends Base, set Object.getPrototypeOf(C.prototype) =
        // Base.prototype. Without this, `(new Derived()) instanceof Base`
        // is false and `Derived.prototype.method` doesn't fall through to
        // Base.prototype.method via prototype-chain walks.
        if (hirClass->baseClass) {
            std::string baseCtorName = hirClass->baseClass->constructor
                ? hirClass->baseClass->constructor->name
                : hirClass->baseClass->name + "_constructor";
            auto baseCtorVal = builder_.createLoadFunction(baseCtorName);
            auto basePropName = builder_.createConstString("prototype");
            auto baseProtoVal = builder_.createCall("ts_object_get_dynamic",
                {baseCtorVal, basePropName}, HIRType::makeAny());
            builder_.createCall("ts_object_setPrototypeOf",
                {proto, baseProtoVal}, HIRType::makeVoid());
        }

        // `prototype` itself stays writable+configurable but intentionally
        // enumerable per spec — keep createSetPropStatic. (This also
        // guarantees the ctor closure's `properties` map exists before the
        // §15.7.14 constructor-proto link below.)
        builder_.createSetPropStatic(ctorVal, "prototype", proto);

        // `class C extends <builtin>`: link C.prototype.[[Proto]] =
        // Builtin.prototype and C.[[Proto]] = Builtin at runtime
        // (ts_class_link_builtin_base no-ops on unknown names). Runs after
        // the prototype install so the ctor's properties map exists.
        if (!hirClass->baseClass && !hirClass->baseBuiltinName.empty()) {
            auto baseNameC = builder_.createConstString(hirClass->baseBuiltinName);
            builder_.createCall("ts_class_link_builtin_base",
                {ctorVal, proto, baseNameC}, HIRType::makeVoid());
        }

        // ECMA-262 §15.7.14: set the derived CONSTRUCTOR's [[Prototype]] to
        // the base constructor, so static members (fields + methods, now own
        // properties of the ctor closure) are inherited via the constructor
        // proto chain. The runtime get_dynamic CLSR branch walks
        // closure->properties' prototype, so `Derived.bf` / `Derived.bm()`
        // fall through to Base. Must run AFTER the prototype install above so
        // closure->properties exists (setPrototypeOf no-ops on a null map).
        if (hirClass->baseClass) {
            std::string baseCtorName2 = hirClass->baseClass->constructor
                ? hirClass->baseClass->constructor->name
                : hirClass->baseClass->name + "_constructor";
            auto baseCtorVal2 = builder_.createLoadFunction(baseCtorName2);
            builder_.createCall("ts_object_setPrototypeOf",
                {ctorVal, baseCtorVal2}, HIRType::makeVoid());
        }

        // Install static methods on the constructor itself so dynamic
        // access like `F.method()` (where `F` is a class-expression-bound
        // variable) resolves to the function. Class declarations have a
        // direct Case 3 dispatch in visitCallExpression that bypasses
        // this, but class expressions and indirect access need it.
        for (auto& [methodName, methodFunc] : hirClass->staticMethods) {
            if (!methodFunc) continue;
            auto methodClosure = builder_.createLoadFunction(completeMethodSymbol(hirClass, methodName, methodFunc, /*isStatic=*/true));
            installMethod(ctorVal, methodName, methodClosure);
        }

        // Install computed-name accessors (`get [expr]()` / `set [expr]()`).
        // ECMA-262 ClassDefinitionEvaluation evaluates each ComputedPropertyName
        // at class-definition time; the resulting property key can't be a
        // static `__getter_<name>` storage key, so the key expression is
        // evaluated and the accessor installed onto the prototype (instance) or
        // the constructor object (static).
        emitComputedAccessorInstalls(hirClass, proto, ctorVal);
    }
    deferredClassPrototypes_.clear();

    // Emit static blocks
    for (auto* staticBlock : deferredStaticBlocks_) {
        for (auto& stmt : staticBlock->body) {
            lowerStatement(stmt.get());
        }
    }
    deferredStaticBlocks_.clear();  // Only emit once
}

void ASTToHIR::generateClassDecoratorStaticInit(const std::string& className,
                                                 const std::vector<ast::Decorator>& classDecorators,
                                                 const std::vector<ast::NodePtr>& members) {
    // Check if there are any decorators (class, method, property, or parameter)
    bool hasDecorators = !classDecorators.empty();
    if (!hasDecorators) {
        for (const auto& member : members) {
            if (auto* method = dynamic_cast<ast::MethodDefinition*>(member.get())) {
                if (!method->decorators.empty()) { hasDecorators = true; break; }
                for (const auto& param : method->parameters) {
                    if (!param->decorators.empty()) { hasDecorators = true; break; }
                }
            } else if (auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                if (!prop->decorators.empty()) { hasDecorators = true; break; }
            }
            if (hasDecorators) break;
        }
    }
    if (!hasDecorators) return;

    SPDLOG_DEBUG("Generating decorator static init for class: {}", className);

    // Create a static init function for this class
    std::string initFuncName = className + "___static_init";
    auto initFunc = std::make_unique<HIRFunction>(initFuncName);

    // Function takes a context parameter (void*) for consistency with legacy
    initFunc->params.push_back({"ctx", HIRType::makePtr()});
    initFunc->returnType = HIRType::makeVoid();
    initFunc->nextValueId = 1;

    // Save current function and set up for the init function
    HIRFunction* savedFunc = currentFunction_;
    // A nested function body starts OUTSIDE any enclosing try/with:
    // tryDepth_/withDepth_ are lowering-state of the OUTER function. A
    // `return` in a closure defined inside a try{} otherwise emits a
    // PopHandler for a handler the closure never pushed — at runtime it
    // popped the CALLER's exception handler (found via the Promise
    // combinator spec-path whose reject-handler vanished).
    int savedTryDepth_fn = tryDepth_; tryDepth_ = 0;
    int savedWithDepth_fn = withDepth_; withDepth_ = 0;
    bool savedWithLexical_fn = withLexical_;
    withLexical_ = withLexical_ || savedWithDepth_fn > 0;
    currentFunction_ = initFunc.get();

    // Create entry block
    auto entryBlock = initFunc->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    pushScope();

    // Create a class descriptor object with 'name' property
    // 1. Create map: ts_map_create() -> map_ptr
    auto classDescriptor = builder_.createCall("ts_map_create", {}, HIRType::makeMap());

    // 2. Create the class name string: ts_string_create("ClassName")
    auto classNameStr = builder_.createConstString(className);

    // 3. Set the 'name' property: ts_map_set_cstr_string(map, "name", classNameStr)
    // Note: key is a C string pointer, value is a TsString*
    auto nameKey = builder_.createConstCString("name");
    builder_.createCall("ts_map_set_cstr_string", {classDescriptor, nameKey, classNameStr}, HIRType::makeVoid());

    // 4. Box the class descriptor: ts_value_make_object(map) -> boxed_descriptor
    auto boxedDescriptor = builder_.createCall("ts_value_make_object", {classDescriptor}, HIRType::makeAny());

    // 5. Call each decorator in reverse order (innermost first, per TypeScript spec)
    for (auto it = classDecorators.rbegin(); it != classDecorators.rend(); ++it) {
        const auto& decorator = *it;

        if (!decorator.expression) continue;

        // If it's a simple identifier (not a factory), call it directly
        if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
            // Decorator function takes (target) and returns target
            // The mangled name is "decoratorName_any" for class decorators
            // Note: Regular functions in HIR don't have an implicit context parameter
            std::string mangledDecoratorName = id->name + "_any";

            SPDLOG_DEBUG("  Calling class decorator: {} (mangled: {})", decorator.name, mangledDecoratorName);

            // Call the decorator: result = decorator_any(boxedDescriptor)
            auto result = builder_.createCall(mangledDecoratorName, {boxedDescriptor}, HIRType::makeAny());

            // For now, we ignore the return value since we can't replace the class in AOT
            (void)result;
        }
        // Handle decorator factories @decorator(args)
        else if (auto* call = dynamic_cast<ast::CallExpression*>(decorator.expression.get())) {
            // Get the factory function name
            auto* factoryIdent = dynamic_cast<ast::Identifier*>(call->callee.get());
            if (!factoryIdent) {
                SPDLOG_DEBUG("  Decorator factory with complex callee not supported: {}", decorator.name);
                continue;
            }

            std::string factoryName = factoryIdent->name;
            SPDLOG_DEBUG("  Calling decorator factory: {}", factoryName);

            // Lower each argument - box them for the _any variant
            std::vector<std::shared_ptr<HIRValue>> factoryArgs;
            for (auto& arg : call->arguments) {
                auto argVal = lowerExpression(arg.get());
                // Box the argument since we're calling the _any variant
                auto boxedArg = boxValueIfNeeded(argVal);
                factoryArgs.push_back(boxedArg);
            }

            // Decorator factories are called with _any suffix since monomorphizer
            // doesn't track decorator usage and generates the _any variant
            std::string mangledFactoryName = factoryName + "_any";
            SPDLOG_DEBUG("    Mangled factory name: {}", mangledFactoryName);

            auto decoratorFunc = builder_.createCall(mangledFactoryName, factoryArgs, HIRType::makeAny());

            // Call the returned decorator with the class descriptor
            auto result = builder_.createCallIndirect(decoratorFunc, {boxedDescriptor}, HIRType::makeAny());
            (void)result;
        }
    }

    // Process property decorators: @decorator on class properties
    // Property decorators receive (target, propertyKey)
    for (const auto& member : members) {
        auto* prop = dynamic_cast<ast::PropertyDefinition*>(member.get());
        if (!prop || prop->decorators.empty()) continue;

        SPDLOG_DEBUG("  Processing property decorators for: {}", prop->name);

        // For _any_str, pass raw TsString* (not boxed)
        auto propertyKey = builder_.createConstString(prop->name);

        for (auto it = prop->decorators.rbegin(); it != prop->decorators.rend(); ++it) {
            const auto& decorator = *it;
            if (!decorator.expression) continue;

            if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                // Property decorator: (target: any, propertyKey: string) -> _any_str
                std::string mangledName = id->name + "_any_str";
                SPDLOG_DEBUG("    Calling property decorator: {} (mangled: {})", id->name, mangledName);
                builder_.createCall(mangledName, {boxedDescriptor, propertyKey}, HIRType::makeVoid());
            }
        }
    }

    // Process method decorators: @decorator on class methods
    // Method decorators receive (target, propertyKey, descriptor)
    for (const auto& member : members) {
        auto* method = dynamic_cast<ast::MethodDefinition*>(member.get());
        if (!method || method->decorators.empty()) continue;

        SPDLOG_DEBUG("  Processing method decorators for: {}", method->name);

        // For _any_str_any, pass raw TsString* for propertyKey (not boxed)
        auto propertyKey = builder_.createConstString(method->name);

        // Create a PropertyDescriptor object appropriate for the member type
        auto descriptorMap = builder_.createCall("ts_map_create", {}, HIRType::makeMap());
        auto trueVal = builder_.createConstInt(1);
        auto boxedTrue = builder_.createCall("ts_value_make_bool", {trueVal}, HIRType::makeAny());

        if (method->isGetter || method->isSetter) {
            // Accessor descriptor: set 'get' and/or 'set' properties
            // For a getter, set 'get' to true; for a setter, set 'set' to true
            // Since getters/setters on the same property share a descriptor, set both
            auto getKey = builder_.createConstCString("get");
            builder_.createCall("ts_map_set_cstr", {descriptorMap, getKey, boxedTrue}, HIRType::makeVoid());
            auto setKey = builder_.createConstCString("set");
            builder_.createCall("ts_map_set_cstr", {descriptorMap, setKey, boxedTrue}, HIRType::makeVoid());
        } else {
            // Data descriptor: set 'value' property
            auto valueKey = builder_.createConstCString("value");
            builder_.createCall("ts_map_set_cstr", {descriptorMap, valueKey, boxedTrue}, HIRType::makeVoid());
        }
        auto boxedDescriptorMap = builder_.createCall("ts_value_make_object", {descriptorMap}, HIRType::makeAny());

        for (auto it = method->decorators.rbegin(); it != method->decorators.rend(); ++it) {
            const auto& decorator = *it;
            if (!decorator.expression) continue;

            if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                // Method decorator: (target: any, propertyKey: string, descriptor: PropertyDescriptor) -> _any_str_any
                std::string mangledName = id->name + "_any_str_any";
                SPDLOG_DEBUG("    Calling method decorator: {} (mangled: {})", id->name, mangledName);
                builder_.createCall(mangledName, {boxedDescriptor, propertyKey, boxedDescriptorMap}, HIRType::makeVoid());
            }
        }
    }

    // Process parameter decorators: @decorator on method parameters
    // Parameter decorators receive (target, propertyKey, parameterIndex)
    for (const auto& member : members) {
        auto* method = dynamic_cast<ast::MethodDefinition*>(member.get());
        if (!method) continue;

        for (size_t paramIdx = 0; paramIdx < method->parameters.size(); ++paramIdx) {
            const auto& param = method->parameters[paramIdx];
            if (param->decorators.empty()) continue;

            SPDLOG_DEBUG("  Processing parameter decorators for: {}[{}]", method->name, paramIdx);

            // For _any_str_int, pass raw TsString* and raw int (not boxed)
            auto propertyKey = builder_.createConstString(method->name);
            auto paramIndex = builder_.createConstInt(static_cast<int64_t>(paramIdx));

            for (auto it = param->decorators.rbegin(); it != param->decorators.rend(); ++it) {
                const auto& decorator = *it;
                if (!decorator.expression) continue;

                if (auto* id = dynamic_cast<ast::Identifier*>(decorator.expression.get())) {
                    // Parameter decorator: (target: any, propertyKey: string, parameterIndex: number) -> _any_str_int
                    std::string mangledName = id->name + "_any_str_int";
                    SPDLOG_DEBUG("    Calling parameter decorator: {} (mangled: {})", id->name, mangledName);
                    builder_.createCall(mangledName, {boxedDescriptor, propertyKey, paramIndex}, HIRType::makeVoid());
                }
            }
        }
    }

    // Return void
    builder_.createReturnVoid();

    popScope();

    // Restore saved function
    currentFunction_ = savedFunc;
    tryDepth_ = savedTryDepth_fn; withDepth_ = savedWithDepth_fn;
    withLexical_ = savedWithLexical_fn;
    if (savedFunc) {
        auto* savedBlock = savedFunc->getEntryBlock();
        if (savedBlock) {
            builder_.setInsertPoint(savedBlock);
            currentBlock_ = savedBlock;
        }
    }

    // Add the static init function to the module
    module_->functions.push_back(std::move(initFunc));

    SPDLOG_DEBUG("Generated decorator static init function: {}", initFuncName);
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
    setSourceLine(node);
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }
}


void ASTToHIR::visitVariableDeclaration(ast::VariableDeclaration* node) {
    setSourceLine(node);
    // VariableDeclaration has name (NodePtr) and initializer (ExprPtr)
    // name can be Identifier, ObjectBindingPattern, or ArrayBindingPattern

    // Lower the initializer first (if any)
    std::shared_ptr<HIRValue> initValue;
    if (node->initializer) {
        // Set pending display name for arrow functions / function expressions
        // so they can use the variable name for .name property (ES2019)
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
            if (dynamic_cast<ast::ArrowFunction*>(node->initializer.get()) ||
                dynamic_cast<ast::FunctionExpression*>(node->initializer.get()) ||
                dynamic_cast<ast::ClassExpression*>(node->initializer.get())) {
                pendingClosureDisplayName_ = ident->name;
            }
        }

        // Pre-register variable for self-referencing function expressions
        // (e.g., `const create = str => { create(match[1]); }` - recursive self-call).
        // The arrow function body is processed inline below, but the variable isn't
        // registered until after lowerExpression returns. Pre-register an alloca so
        // that isCapturedVariable/lookupVariableInfo can find it during body lowering.
        std::shared_ptr<HIRValue> preAllocaPtr;
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->name.get())) {
            if (dynamic_cast<ast::ArrowFunction*>(node->initializer.get()) ||
                dynamic_cast<ast::FunctionExpression*>(node->initializer.get())) {
                // Only pre-register if not already in scope (avoid duplicates)
                if (!lookupVariableInfo(ident->name)) {
                    preAllocaPtr = builder_.createAlloca(HIRType::makeAny(), ident->name);
                    defineVariableAlloca(ident->name, preAllocaPtr, HIRType::makeAny());
                }
            }
        }

        SPDLOG_DEBUG("[VD] initializer kind={} for var={}", node->initializer->getKind(),
            dynamic_cast<ast::Identifier*>(node->name.get()) ? dynamic_cast<ast::Identifier*>(node->name.get())->name : "?");
        initValue = lowerExpression(node->initializer.get());
        pendingClosureDisplayName_.clear();

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
        // Override: if initValue is a Generator (from generator method call), use that type
        // The analyzer may infer the wrong class type (e.g., NumberRange instead of Generator)
        if (initValue && initValue->type && initValue->type->kind == HIRTypeKind::Class &&
            initValue->type->className == "Generator") {
            varType = initValue->type;
        }

        // Decide whether to reuse an existing alloca or create a fresh one:
        //  - `var` is function-scoped & hoisted: reuse the pre-hoisted alloca in
        //    the current function (a `var` in a nested function still shadows via
        //    lookupVariableInfoInCurrentFunction's function-boundary stop).
        //  - `let`/`const` is block-scoped: only reuse if already declared in THIS
        //    innermost scope; otherwise create a FRESH alloca so it SHADOWS any
        //    outer same-named binding. Without this, the inner `let x` in
        //    `for (let x=0; x<10;) { x++; { let x="hi"; } }` reused the loop var's
        //    slot, clobbered the counter with a string, and looped forever.
        VariableInfo* existingInfo = nullptr;
        if (node->varKind == ast::VarKind::Var) {
            existingInfo = lookupVariableInfoInCurrentFunction(ident->name);
        } else if (!scopes_.empty()) {
            auto it = scopes_.back().variables.find(ident->name);
            if (it != scopes_.back().variables.end()) existingInfo = &it->second;
        }
        if (existingInfo && existingInfo->isAlloca) {
            // Inside a `with` body, `var x = init` initializes THROUGH the
            // scope chain (ES 14.3.2 -> PutValue): when the innermost
            // with-object has `x`, the write lands there and the hoisted var
            // stays untouched. ts_with_try_set reports whether a with-object
            // took the value; the static store runs only on the false branch.
            if (withScopeActive() && node->varKind == ast::VarKind::Var && initValue) {
                auto nameC = builder_.createConstString(ident->name);
                auto wrote = builder_.createCall("ts_with_try_set",
                    {nameC, boxValueIfNeeded(initValue)}, HIRType::makeAny());
                int bid = blockCounter_++;
                auto* storeBB = createBlock("withvar.store" + std::to_string(bid));
                auto* contBB = createBlock("withvar.cont" + std::to_string(bid));
                builder_.createCondBranch(wrote, contBB, storeBB);
                builder_.setInsertPoint(storeBB);
                currentBlock_ = storeBB;
                builder_.createStore(initValue, existingInfo->value, varType);
                broadcastCaptureWrite(existingInfo, initValue);
                builder_.createBranch(contBB);
                builder_.setInsertPoint(contBB);
                currentBlock_ = contBB;
                if (varType->kind != HIRTypeKind::Any) {
                    existingInfo->elemType = varType;
                }
                return;
            }
            // Variable was pre-hoisted: just store the init value into the existing alloca
            builder_.createStore(initValue, existingInfo->value, varType);
            // Update the type info if we have a more specific type now
            if (varType->kind != HIRTypeKind::Any) {
                existingInfo->elemType = varType;
            }
            // If this variable is captured by nested closures, propagate the
            // assignment to every capture cell (lodash declares many helpers
            // that all capture the same forward-referenced var).
            broadcastCaptureWrite(existingInfo, initValue);
        } else {
            auto allocaPtr = builder_.createAlloca(varType, ident->name);
            builder_.createStore(initValue, allocaPtr, varType);
            defineVariableAlloca(ident->name, allocaPtr, varType);
        }

        // If this variable is a module-scoped global (from an imported module),
        // also store the value to the LLVM global variable so other functions
        // from the same module can access it via LoadGlobal.
        // Only do this in the module init function itself — a `var` declaration
        // inside a nested function expression (e.g., forEach callback) with the
        // same name should shadow the module global, not overwrite it.
        if (isModuleGlobalVar(ident->name) &&
            (!currentFunction_ || currentFunction_->name.find("__module_init_") == 0)) {
            std::string globalName = modVarName(ident->name);
            builder_.createStoreGlobal(globalName, initValue);
        }
    } else if (auto* objPattern = dynamic_cast<ast::ObjectBindingPattern*>(node->name.get())) {
        // Object destructuring: const { a, b } = obj
        lowerObjectBindingPattern(objPattern, initValue);
    } else if (auto* arrPattern = dynamic_cast<ast::ArrayBindingPattern*>(node->name.get())) {
        // Array destructuring: const [a, b] = arr
        lowerArrayBindingPattern(arrPattern, initValue);
    }
}

void ASTToHIR::emitInstanceFieldSet(std::shared_ptr<HIRValue> thisValue,
                                    ast::PropertyDefinition* propDef,
                                    std::shared_ptr<HIRValue> initVal) {
    // ECMA-262 15.7.x: a computed field name (`["a"+"b"] = v`) evaluates the
    // key expression at instance-init time. The parser stores the
    // ComputedPropertyName in propDef->nameNode and leaves name=="[computed]".
    if (propDef->name == "[computed]" && propDef->nameNode) {
        if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(propDef->nameNode.get())) {
            std::shared_ptr<HIRValue> key;
            // SYNTHETIC default ctors lower in the early class pre-pass,
            // BEFORE moduleGlobalVarsByModule_ is populated — a bare
            // module-var key (`class C { [s] = 1 }` with top-level
            // `var s = Symbol()`) lowered to undefined and the field stored
            // under the string "undefined". When the key is an identifier
            // with no local binding, read the __modvar_ global directly
            // (module init StoreGlobals every module-scoped var there).
            if (auto* kid = dynamic_cast<ast::Identifier*>(cpn->expression.get())) {
                if (!lookupVariable(kid->name) && !isModuleGlobalVar(kid->name)) {
                    key = builder_.createLoadGlobal(modVarName(kid->name));
                }
            }
            if (!key) key = lowerExpression(cpn->expression.get());
            builder_.createSetPropDynamic(thisValue, key, std::move(initVal));
            return;
        }
    }
    builder_.createSetPropStatic(thisValue, privateStorageKey(propDef->name), std::move(initVal));
}

void ASTToHIR::lowerObjectBindingPattern(ast::ObjectBindingPattern* pattern,
                                          std::shared_ptr<HIRValue> sourceValue) {
    // ECMA-262 8.6.2 BindingInitialization, `ObjectBindingPattern : { }` and
    // `{ BindingPropertyList }` BOTH step 1: Perform ? RequireObjectCoercible(value).
    // The empty `{}` pattern coerces too — `function f({}){}; f(undefined)` and
    // `var {} = undefined` throw TypeError (matches Node + the array path below,
    // which checks unconditionally). Always perform the check.
    builder_.createCall("ts_destructure_require_object", {sourceValue},
                        HIRType::makeVoid());
    // Track static keys consumed by non-rest elements, to build the exclusion
    // set for a trailing `...rest` (ECMA-262 14.6 / RestBindingInitialization).
    std::vector<std::shared_ptr<HIRValue>> consumedKeys;
    for (auto& elem : pattern->elements) {
        auto* binding = dynamic_cast<ast::BindingElement*>(elem.get());
        if (!binding) continue;
        if (binding->isSpread) {
            // `{ ...rest }` — own enumerable props of source minus consumed keys.
            auto keysArr = builder_.createCall(
                "ts_array_create", {},
                HIRType::makeArray(HIRType::makeAny(), false));
            for (auto& k : consumedKeys) {
                builder_.createCall("ts_array_push", {keysArr, k},
                                    HIRType::makeVoid());
            }
            auto restObj = builder_.createCall(
                "ts_object_rest_exclude", {sourceValue, keysArr},
                HIRType::makeAny());
            if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
                auto varType = HIRType::makeAny();
                auto allocaPtr = builder_.createAlloca(varType, ident->name);
                builder_.createStore(restObj, allocaPtr, varType);
                defineVariableAlloca(ident->name, allocaPtr, varType);
                if (isModuleGlobalVar(ident->name)) {
                    builder_.createStoreGlobal(modVarName(ident->name), restObj);
                }
            }
            continue;
        }
        // Record this element's (static) key for the rest-exclusion set. Computed
        // keys are intentionally not collected here to avoid double-evaluating
        // the key expression (rare with rest; an accepted edge).
        if (!binding->propertyName.empty()) {
            consumedKeys.push_back(builder_.createConstString(binding->propertyName));
        } else if (auto* id = dynamic_cast<ast::Identifier*>(binding->name.get())) {
            consumedKeys.push_back(builder_.createConstString(id->name));
        }
        lowerBindingElement(binding, sourceValue, true /* isObjectPattern */);
    }
}

void ASTToHIR::lowerArrayBindingPattern(ast::ArrayBindingPattern* pattern,
                                         std::shared_ptr<HIRValue> sourceValue) {
    // ECMA-262 8.5.2 BindingInitialization for ArrayBindingPattern uses
    // GetIterator, which throws TypeError when the source is null or
    // undefined (no @@iterator on those). Even for `[] = null` an empty
    // pattern still constructs an iterator, so the check applies
    // unconditionally.
    builder_.createCall("ts_destructure_require_object", {sourceValue},
                        HIRType::makeVoid());

    // ECMA-262 13.3.3.6: `ArrayBindingPattern : [ ]` returns NormalCompletion
    // immediately — it performs NO iteration of the source. Calling the source's
    // @@iterator (which, for a generator, advances it once in this runtime)
    // would be observable (`class C{ method([]){} }; new C().method(gen())`
    // must leave the generator un-iterated). Stop after the nullish guard.
    if (pattern->elements.empty()) {
        return;
    }

    // ECMA-262 8.5.2: ArrayBindingPattern uses the ITERATOR protocol, not index
    // access. Materialize the iterator's values into a real array up front, then
    // extract by index from that materialized array. This is what makes
    // `[a,b] = anyIterable` work for non-array iterables (generators, Maps, user
    // [Symbol.iterator] objects, strings) — index access on those yields
    // undefined. Compute how many values to pull: all of them when a rest
    // element is present, else exactly the (non-rest) element count (holes still
    // consume an iterator step).
    int64_t consumeCount = 0;
    bool hasRest = false;
    for (auto& elem : pattern->elements) {
        if (auto* be = dynamic_cast<ast::BindingElement*>(elem.get())) {
            if (be->isSpread) { hasRest = true; break; }
        }
        consumeCount++;
    }
    auto materialized = builder_.createCall(
        "ts_destructure_iterate",
        { sourceValue, builder_.createConstInt(consumeCount),
          builder_.createConstInt(hasRest ? 1 : 0) },
        HIRType::makeArray(HIRType::makeAny()));

    int64_t index = 0;
    for (auto& elem : pattern->elements) {
        if (auto* binding = dynamic_cast<ast::BindingElement*>(elem.get())) {
            if (binding->isSpread) {
                // Rest element: ...rest - remaining materialized elements
                lowerRestElement(binding, materialized, index);
            } else {
                // Regular element - extract by index from the materialized array
                lowerBindingElementByIndex(binding, materialized, index);
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
        // Lazily evaluate the default ONLY when the property is undefined.
        // ECMA-262 KeyedBindingInitialization: the Initializer is evaluated in
        // the "v is undefined" step, so a SKIPPED default must NOT run its side
        // effects (and an unresolvable-ref default must not throw). The old
        // createSelect evaluated both operands eagerly. Mirror bindOneParameter.
        auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "dstr_dflt");
        auto* defaultBB = currentFunction_->createBlock("dstr_default");
        auto* usedBB = currentFunction_->createBlock("dstr_used");
        auto* mergeBB = currentFunction_->createBlock("dstr_merge");
        builder_.createCondBranch(isUndefined, defaultBB, usedBB);

        builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
        // ECMA-262 NamedEvaluation: an anonymous function/arrow/class default
        // takes the binding name (`{ x = () => {} } = {}` → x.name === "x").
        // Kept entirely inside the default block so it can't leak when skipped.
        std::string savedPCDN = pendingClosureDisplayName_;
        if (auto* bid = dynamic_cast<ast::Identifier*>(binding->name.get())) {
            auto* init = binding->initializer.get();
            if (dynamic_cast<ast::ArrowFunction*>(init) ||
                dynamic_cast<ast::FunctionExpression*>(init) ||
                dynamic_cast<ast::ClassExpression*>(init)) {
                pendingClosureDisplayName_ = bid->name;
            }
        }
        auto defaultValue = lowerExpression(binding->initializer.get());
        pendingClosureDisplayName_ = savedPCDN;
        defaultValue = boxValueIfNeeded(defaultValue);
        builder_.createStore(defaultValue, mergeSlot);
        builder_.createBranch(mergeBB);

        builder_.setInsertPoint(usedBB); currentBlock_ = usedBB;
        builder_.createStore(extractedValue, mergeSlot);
        builder_.createBranch(mergeBB);

        builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
        extractedValue = builder_.createLoad(HIRType::makeAny(), mergeSlot);
    }

    // Bind to variable(s)
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        // Simple variable binding
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(extractedValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);

        // If this variable is a module-scoped global (e.g. from destructured require()),
        // also store to the __modvar_ LLVM global so other functions can access it.
        if (isModuleGlobalVar(ident->name)) {
            builder_.createStoreGlobal(modVarName(ident->name), extractedValue);
        }
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
        // ECMAScript array-destructuring default: the default applies when the
        // element is `undefined` — i.e. the index is OUT OF BOUNDS *or* the
        // in-bounds element is a hole / explicit undefined. A bounds check alone
        // (the old lowering) missed the in-bounds-undefined case, so
        // `[x = 1] = [undefined]` and `[x = 1] = [,]` wrongly yielded undefined.
        // The element read (createGetElem above) is unchecked and returns garbage
        // out of bounds, so we keep an explicit bounds guard AND an is-undefined
        // check; extractedValue is boxed below so ts_value_is_undefined is valid.
        auto arrayLength = builder_.createCall("ts_array_length", {sourceValue}, HIRType::makeInt64());
        auto idxConst = builder_.createConstInt(index);
        auto notInBounds = builder_.createCmpGe(idxConst, arrayLength);  // index >= length
        // Box the extracted value if it was unboxed by type propagation, then
        // test undefined. (For an out-of-bounds index this is garbage, but the
        // notInBounds branch is taken first, so isUndef is only consulted when
        // the element is in bounds.)
        extractedValue = boxValueIfNeeded(extractedValue);
        auto isUndef = builder_.createIsUndefined(extractedValue);

        // useDefault = notInBounds || isUndefined(element). Lazily evaluate the
        // default ONLY in that case — a SKIPPED default must not run its side
        // effects (and an unresolvable-ref default must not throw). The old
        // nested createSelect evaluated the default eagerly. Mirror bindOneParameter.
        auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "dstr_dflt");
        auto* checkBB = currentFunction_->createBlock("dstr_chk");
        auto* defaultBB = currentFunction_->createBlock("dstr_default");
        auto* usedBB = currentFunction_->createBlock("dstr_used");
        auto* mergeBB = currentFunction_->createBlock("dstr_merge");
        builder_.createCondBranch(notInBounds, defaultBB, checkBB);

        builder_.setInsertPoint(checkBB); currentBlock_ = checkBB;
        builder_.createCondBranch(isUndef, defaultBB, usedBB);

        builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
        // ECMA-262 NamedEvaluation (`[ x = () => {} ] = []` → x.name === "x"),
        // kept inside the default block so it can't leak when the default is skipped.
        std::string savedPCDN = pendingClosureDisplayName_;
        if (auto* bid = dynamic_cast<ast::Identifier*>(binding->name.get())) {
            auto* init = binding->initializer.get();
            if (dynamic_cast<ast::ArrowFunction*>(init) ||
                dynamic_cast<ast::FunctionExpression*>(init) ||
                dynamic_cast<ast::ClassExpression*>(init)) {
                pendingClosureDisplayName_ = bid->name;
            }
        }
        auto defaultValue = lowerExpression(binding->initializer.get());
        pendingClosureDisplayName_ = savedPCDN;
        defaultValue = boxValueIfNeeded(defaultValue);
        builder_.createStore(defaultValue, mergeSlot);
        builder_.createBranch(mergeBB);

        builder_.setInsertPoint(usedBB); currentBlock_ = usedBB;
        builder_.createStore(extractedValue, mergeSlot);
        builder_.createBranch(mergeBB);

        builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
        extractedValue = builder_.createLoad(HIRType::makeAny(), mergeSlot);
    }

    // Bind to variable(s)
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(extractedValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);

        // If this variable is a module-scoped global (e.g. from destructured require()),
        // also store to the __modvar_ LLVM global so other functions can access it.
        if (isModuleGlobalVar(ident->name)) {
            builder_.createStoreGlobal(modVarName(ident->name), extractedValue);
        }
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

    // Bind to a variable, or destructure the rest array into a nested pattern.
    // ECMA-262 BindingRestElement may be a BindingPattern (`[...{ length }]`,
    // `[...[a, b]]`), not just a BindingIdentifier — previously only the
    // identifier case bound, so the pattern forms silently left their targets
    // undefined.
    if (auto* ident = dynamic_cast<ast::Identifier*>(binding->name.get())) {
        auto varType = HIRType::makeAny();
        auto allocaPtr = builder_.createAlloca(varType, ident->name);
        builder_.createStore(restValue, allocaPtr, varType);
        defineVariableAlloca(ident->name, allocaPtr, varType);
    } else if (auto* objPat =
                   dynamic_cast<ast::ObjectBindingPattern*>(binding->name.get())) {
        lowerObjectBindingPattern(objPat, restValue);
    } else if (auto* arrPat =
                   dynamic_cast<ast::ArrayBindingPattern*>(binding->name.get())) {
        lowerArrayBindingPattern(arrPat, restValue);
    }
}



} // namespace ts::hir
