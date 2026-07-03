#include <algorithm>
#include "ASTToHIR_Internal.h"

namespace ts::hir {

// ECMA-262 directive prologue: exact SOURCE-TEXT "use strict" (escaped
// variants do not count). Shared by every function-lowering path so the
// per-function strictCode_ (which gates sloppy-this coercion) is uniform.
static bool bodyHasUseStrictDirective(const std::vector<std::unique_ptr<ast::Statement>>& body) {
    for (auto& st : body) {
        auto* es = dynamic_cast<ast::ExpressionStatement*>(st.get());
        if (!es) break;
        auto* sl = dynamic_cast<ast::StringLiteral*>(es->expression.get());
        if (!sl) break;
        if (sl->raw == "\"use strict\"" || sl->raw == "'use strict'" ||
            (sl->raw.empty() && sl->value == "use strict"))
            return true;
    }
    return false;
}

void ASTToHIR::visitFunctionDeclaration(ast::FunctionDeclaration* node) {
    bool savedStrict_decl = strictCode_;
    if (bodyHasUseStrictDirective(node->body)) strictCode_ = true;
    struct StrictRestoreDecl {
        bool* p; bool v;
        ~StrictRestoreDecl() { *p = v; }
    } _strictRestoreDecl{&strictCode_, savedStrict_decl};

    setSourceLine(node);
    SPDLOG_DEBUG("[FD] ENTER name={} scopes={} currentFunc={} bodySize={}",
        node->name, scopes_.size(),
        currentFunction_ ? currentFunction_->name : "null",
        node->body.size());
    // Note: empty-body functions (e.g., `function F() {}`) are valid JS functions
    // that return undefined. We still create a closure for them so typeof/instanceof
    // work correctly. The body loop below will simply not emit any statements.

    // Create HIR function - HIRFunction constructor requires a name
    // Add module hash suffix for cross-module disambiguation when inside a
    // module init function (JS modules may define functions with the same name).
    // Generate a unique name for the function declaration. Inner function
    // declarations can collide across modules (e.g., `function next()` inside
    // `handle()` in both route.js and router/index.js). Use a counter to ensure
    // uniqueness, similar to how function expressions use funcExprCounter_.
    std::string funcName = node->name;
    if (currentFunction_) {
        // Nested function declaration — always disambiguate with a counter
        funcName += "_" + std::to_string(funcExprCounter_++);
    } else if (!currentModulePath_.empty()) {
        // Top-level module function — disambiguate with module hash
        std::hash<std::string> hasher;
        auto hash = hasher(currentModulePath_) % 999999;
        funcName += "_m" + std::to_string(hash);
    }
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = node->isGenerator;
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;
    func->displayName = node->name;

    // Collect destructured parameter patterns for later extraction
    struct DestructuredParam {
        size_t paramIndex;
        ast::ObjectBindingPattern* objPattern = nullptr;
        ast::ArrayBindingPattern* arrPattern = nullptr;
        ast::Node* defaultInitializer = nullptr;
    };
    std::vector<DestructuredParam> destructuredParams;

    // Handle parameters
    for (size_t paramIdx = 0; paramIdx < node->parameters.size(); ++paramIdx) {
        auto& param = node->parameters[paramIdx];
        // Convert parameter type from string if available
        auto paramType = param->type.empty()
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        // If parameter has a default value, it must be Any type to receive undefined
        if (param->initializer) {
            paramType = HIRType::makeAny();
        }

        // Get parameter name from NodePtr (it's a unique_ptr<Node>)
        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            // Force Any type for destructured params (we extract properties at function entry)
            paramType = HIRType::makeAny();
            destructuredParams.push_back({func->params.size(), objPat, nullptr,
                param->initializer.get()});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            destructuredParams.push_back({func->params.size(), nullptr, arrPat,
                param->initializer.get()});
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

    // If the function body uses 'arguments', add hidden __argN__ params
    // so call args beyond the declared param count can flow through the
    // trampoline into ts_create_arguments_from_params. Without this,
    // top-level functions called with extra args see arguments[N]
    // resolve to undefined because direct_0's LLVM signature has no
    // slot to receive them.
    {
        bool bodyUsesArgs = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                bodyUsesArgs = true;
                break;
            }
        }
        if (!bodyUsesArgs) bodyUsesArgs = paramsReferenceArguments(node->parameters);
        if (bodyUsesArgs) {
            while (func->params.size() < 10) {
                std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                func->params.push_back({argName, HIRType::makeAny()});
            }
        }
    }

    // Use declared return type if available, otherwise default to Any
    func->returnType = node->returnType.empty()
        ? HIRType::makeAny()
        : convertTypeFromString(node->returnType);

    // Save current function AND current block (needed for nested functions in try/catch)
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
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures
    auto savedInnerFuncClosures = std::move(innerFuncClosures_);
    innerFuncClosures_.clear();
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

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

    // Register parameters in the scope so they can be looked up.
    // Parameter values have IDs 0, 1, 2, ... matching their index in HIRToLLVM.
    preseedParamTDZ(func.get(), node->parameters);
    // Strategy B Phase 6a: per-parameter logic factored into bindOneParameter.
    for (size_t i = 0; i < func->params.size(); ++i) {
        ast::Parameter* astParam = (i < node->parameters.size()) ? node->parameters[i].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/true);
    }

    // Emit destructuring extraction for parameters with binding patterns.
    // Strategy B Phase 6a: per-parameter logic factored into extractDestructuringForParam.
    for (auto& dp : destructuredParams) {
        extractDestructuringForParam(func.get(), dp.paramIndex,
            dp.objPattern, dp.arrPattern, dp.defaultInitializer);
    }

    // Async generators (eager-body model): mark the end of the PARAMETER
    // prologue. Param-binding errors before this marker must throw
    // SYNCHRONOUSLY out of gen() (dstr/dflt-params family); body throws
    // after it reject the first next() promise per spec. The agen.reject
    // landing pad consults gen->bodyStarted (ts_agen_should_reject).
    if (func->isAsync && func->isGenerator) {
        builder_.createCall("ts_async_generator_body_started", {},
                            HIRType::makeVoid());
    } else if (func->isGenerator) {
        // Sync generator: eager-parameter model (marker = suspension 0 -> 1).
        builder_.createCall("ts_generator_body_started", {},
                            HIRType::makeVoid());
    } else if (func->isGenerator) {
        // Sync generators: same eager-parameter model. The marker ends the
        // parameter prologue; the wrapper invokes the impl once at gen() time
        // so param-binding/default throws escape gen() synchronously (ECMA-262
        // FunctionDeclarationInstantiation runs at call time, before the
        // generator object is returned), while the body stays lazy until the
        // first next(). HIRToLLVM lowers this marker as suspension 0 -> 1.
        builder_.createCall("ts_generator_body_started", {},
                            HIRType::makeVoid());
    }

    // Create 'arguments' array-like object if the function body references 'arguments'.
    // Must be done at function entry (before any other code) because inner calls
    // will overwrite ts_last_call_argc, making lazy creation incorrect.
    // Arrow functions don't have their own 'arguments' (they inherit from enclosing).
    {
        bool usesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                usesArguments = true;
                break;
            }
        }
        if (!usesArguments) usesArguments = paramsReferenceArguments(node->parameters);
        if (usesArguments) {
            // Build args for ts_create_arguments_from_params(p0..p9)
            // The runtime uses ts_last_call_argc to know how many were actually passed.
            // Include both declared params AND hidden __argN__ params so that
            // functions with fewer formal params than call args still capture all args.
            std::vector<std::shared_ptr<HIRValue>> callArgs;

            // Pass each user parameter (up to 10), padding with undefined
            size_t userIdx = 0;
            for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
                if (func->params[i].first == "__closure__") continue;
                auto paramVal = lookupVariable(func->params[i].first);
                if (!paramVal) {
                    paramVal = builder_.createConstUndefined();
                }
                callArgs.push_back(paramVal);
                userIdx++;
            }
            // Pad remaining slots with undefined (up to 10 total params)
            while (userIdx < 10) {
                callArgs.push_back(builder_.createConstUndefined());
                userIdx++;
            }

            // Call runtime to create arguments array
            auto argsArray = builder_.createCall("ts_create_arguments_from_params",
                callArgs, HIRType::makeAny());

            // Store as local variable "arguments"
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
            builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
            defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
        }
    }

    // JavaScript function hoisting: pre-declare nested function names as variables
    // This allows functions to be called before they appear in source order.
    // We create allocas for function names, which will be filled when the function
    // declaration is processed. Calls to these names will use indirect call.
    for (auto& stmt : node->body) {
        if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            // Create a function type for the closure
            auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
            for (auto& param : funcDecl->parameters) {
                std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                }
                funcType->paramTypes.push_back(paramType);
            }
            funcType->returnType = funcDecl->returnType.empty()
                ? HIRType::makeAny()
                : convertTypeFromString(funcDecl->returnType);

            // Create an alloca for the function variable (will hold closure or function ptr)
            auto allocaVal = builder_.createAlloca(funcType, funcDecl->name);
            // Initialize with null - will be set when the function is processed
            builder_.createStore(builder_.createConstNull(), allocaVal);
            defineVariableAlloca(funcDecl->name, allocaVal, funcType);
            // Function-name slot: Annex-B var-copy target for block fns.
            if (auto* vi_ = lookupVariableInfoInCurrentFunction(funcDecl->name))
                vi_->isFnHoist = true;
            // Function-name slot: a block-level `function f` may Annex-B
            // var-copy into it (block-decl-global-existing-fn-update).
            if (auto* vi = lookupVariableInfoInCurrentFunction(funcDecl->name))
                vi->isFnHoist = true;
        }
    }

    // ECMA-262 §14.3.2: hoist every `var` declaration in the function
    // body — including those buried inside if/else, loops, try, switch
    // — to the enclosing FunctionEnvironment. Without this, assignments
    // like `} else if (...) { result = x; }` after a `var result =`
    // in a different branch bind to a fresh slot per branch (or the
    // global object) and the surrounding function sees `undefined`.
    {
        std::vector<std::string> hoistedVars;
        std::vector<std::string> hoistedFns;
        for (auto& stmt : node->body) {
            collectHoistedVarNames(stmt.get(), hoistedVars, &hoistedFns);
        }
        {
            // Annex B B.3.3: suppress the var-copy for fn names that clash
            // with a top-level lexical declaration.
            std::set<std::string> lexNames_;
            collectTopLevelLexicalNames(node->body, lexNames_);
            for (auto& fn_ : hoistedFns)
                if (lexNames_.count(fn_))
                    hoistedVars.erase(std::remove(hoistedVars.begin(), hoistedVars.end(), fn_), hoistedVars.end());
            hoistedFns.erase(std::remove_if(hoistedFns.begin(), hoistedFns.end(),
                [&](const std::string& x){ return lexNames_.count(x) != 0; }), hoistedFns.end());
        }
        for (auto& name : hoistedVars) {
            if (lookupVariableInfoInCurrentFunction(name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
            builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
            defineVariableAlloca(name, allocaVal, HIRType::makeAny());
            if (std::find(hoistedFns.begin(), hoistedFns.end(), name) != hoistedFns.end())
                if (auto* vi = lookupVariableInfoInCurrentFunction(name)) vi->isFnHoist = true;
        }
    }

    // Pre-declare top-level `let` / `const` so nested FunctionDeclaration
    // bodies (lowered in pass 1 below, BEFORE the let initializers run
    // in pass 2) can resolve outer-scope captures. Without this,
    // `function outer() { let count = 0; function inner() { count++; } }`
    // lowers inner's body when `count` isn't yet in scope, so inner
    // resolves `count` to const-undefined and the closure has no
    // captures — outer's count stays 0 forever. Block-scoped lets
    // nested inside if / loops are intentionally NOT pre-declared
    // (they have their own block scope and would shadow the wrong
    // way at the function level).
    for (auto& stmt : node->body) {
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
            auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
            if (!ident) continue;
            if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
            // TDZ sentinel (not undefined): a read before the declaration
            // initializes the slot throws ReferenceError via ts_tdz_check.
            auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
            builder_.createStore(tdz, allocaVal, HIRType::makeAny());
            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
            if (auto* vi = lookupVariableInfoInCurrentFunction(ident->name)) vi->isTDZ = true;
        }
    }

    // If this is user_main (or the synthetic equivalent for top-level scripts),
    // emit deferred static property initializations and class prototype installs.
    // This must run AFTER the var/let/const hoisting above (which creates the
    // function-local slots) but before the body: a class's computed accessor key
    // (e.g. `get [_ = 'str'+'ing']()`) is evaluated here and may assign to a
    // hoisted variable — if its slot doesn't exist yet the write crashes.
    if (node->name == "user_main" || node->name == "__synthetic_user_main") {
        emitDeferredStaticInits();
    }

    // Lower function body in two passes for proper JavaScript function hoisting:
    // FIRST PASS: Process FunctionDeclarations to create closures
    // This ensures nested functions are available before any other code runs,
    // matching JavaScript semantics where function declarations are hoisted.
    for (size_t i = 0; i < node->body.size(); ++i) {
        auto& stmt = node->body[i];
        if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
        }
    }
    emitMutualRecursionFixup();

    // SECOND PASS: Process non-FunctionDeclaration statements in order
    for (size_t i = 0; i < node->body.size(); ++i) {
        auto& stmt = node->body[i];
        if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
            // Stop processing statements after a terminator (return, throw, etc.)
            // This prevents dead code from being emitted after control flow ends
            if (builder_.isBlockTerminated()) {
                break;
            }
        }
    }

    // Add implicit return if no terminator
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

    // Get the function pointer before we move it
    HIRFunction* funcPtr = func.get();

    // Restore saved function and block
    currentFunction_ = savedFunc;
    tryDepth_ = savedTryDepth_fn; withDepth_ = savedWithDepth_fn;
    withLexical_ = savedWithLexical_fn;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    innerFuncClosures_ = std::move(savedInnerFuncClosures);
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
    if (savedBlock) {
        builder_.setInsertPoint(savedBlock);
    }

    // Add function to module
    module_->functions.push_back(std::move(func));

    // For nested functions (when savedFunc != nullptr), we need to handle closures
    // If this is a nested function with captures, create a closure and define
    // the function name as a closure variable in the outer scope
    if (savedFunc && hasClosure) {
        // Build function type for the closure
        auto closureFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
        for (const auto& [paramName, paramType] : funcPtr->params) {
            closureFuncType->paramTypes.push_back(paramType);
        }
        closureFuncType->returnType = funcPtr->returnType;

        // Create a closure with captured values
        std::vector<std::shared_ptr<HIRValue>> captureValues;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            const auto& capType = cap.second;

            // Check if this variable requires capture propagation
            size_t scopeIndex = 0;
            bool needsCapturePropagation = isCapturedVariable(capName, &scopeIndex);

            if (needsCapturePropagation) {
                // Variable is in an outer function's scope - propagate the capture
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
                    captureValues.push_back(builder_.createConstNull());
                }
            }
        }

        auto closureVal = builder_.createMakeClosure(funcName, captureValues, closureFuncType);

        // Mark captured variables as "captured by nested" and register the
        // closure cell so later writes can propagate. When a variable is
        // captured by MULTIPLE nested closures (e.g., lodash's `upperFirst`
        // referenced from many helper closures), record each one in
        // additionalCaptures — write sites iterate all of them so every
        // closure sees the assignment.
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(closureVal, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = captureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, captureIdx);
                    }
                }
            }
            captureIdx++;
        }

        // Store the closure into the pre-created alloca (if it exists)
        // This enables function hoisting - the alloca was created before processing statements
        // Block-level function declaration semantics (ES 14.2.3 + Annex B
        // B.3.3): inside a BLOCK, the declaration binds BLOCK-locally; the
        // function-scope var-copy goes ONLY into the dedicated hoist slot
        // (VariableInfo::isFnHoist, absent when a lexical collision
        // suppressed it). Storing into an arbitrary outer binding clobbered
        // same-named let/const (e.g. `for (let f in ...) { { function f(){} } }`
        // overwrote the loop variable and leaked f past the loop).
        bool fdInBlock = false;
        for (size_t si = scopes_.size(); si-- > 0;) {
            if (scopes_[si].isFunctionBoundary) {
                fdInBlock = (si != scopes_.size() - 1);
                break;
            }
        }
        auto* existingInfo = lookupVariableInfo(node->name);
        bool storeToExisting = existingInfo && existingInfo->isAlloca &&
                               (!fdInBlock || existingInfo->isFnHoist);
        if (storeToExisting) {
            builder_.createStore(closureVal, existingInfo->value);
            broadcastCaptureWrite(existingInfo, closureVal);
        }
        if (fdInBlock || !storeToExisting) {
            // Block-local binding (or a fresh top-level one).
            defineVariable(node->name, closureVal);
        }

        // Record closure info for mutual recursion post-sweep
        {
            InnerFuncClosureInfo closureInfo;
            closureInfo.funcName = node->name;
            closureInfo.closureValue = closureVal;
            int idx = 0;
            for (const auto& cap : innerCaptures) {
                closureInfo.captureNamesAndIndices.push_back({cap.first, idx++});
            }
            innerFuncClosures_.push_back(std::move(closureInfo));
        }

        // Also store to module global if this is a module-level function declaration.
        // This allows other functions in the module to access it via __modvar_ globals
        // instead of closure cells, fixing ordering issues where a capturing function
        // is declared before the captured function.
        if (isModuleGlobalVar(node->name)) {
            builder_.createStoreGlobal(modVarName(node->name), closureVal);
        }
    } else if (savedFunc) {
        // Nested function without captures - still store it so it can be called
        // Build function type
        auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
        for (const auto& [paramName, paramType] : funcPtr->params) {
            funcType->paramTypes.push_back(paramType);
        }
        funcType->returnType = funcPtr->returnType;

        // Create a closure with no captures (for call_indirect compatibility)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        auto closureVal = builder_.createMakeClosure(funcName, emptyCaptureValues, funcType);

        // Store into pre-created alloca or define new variable
        // Block-level function declaration semantics (ES 14.2.3 + Annex B
        // B.3.3): inside a BLOCK, the declaration binds BLOCK-locally; the
        // function-scope var-copy goes ONLY into the dedicated hoist slot
        // (VariableInfo::isFnHoist, absent when a lexical collision
        // suppressed it). Storing into an arbitrary outer binding clobbered
        // same-named let/const (e.g. `for (let f in ...) { { function f(){} } }`
        // overwrote the loop variable and leaked f past the loop).
        bool fdInBlock = false;
        for (size_t si = scopes_.size(); si-- > 0;) {
            if (scopes_[si].isFunctionBoundary) {
                fdInBlock = (si != scopes_.size() - 1);
                break;
            }
        }
        auto* existingInfo = lookupVariableInfo(node->name);
        bool storeToExisting = existingInfo && existingInfo->isAlloca &&
                               (!fdInBlock || existingInfo->isFnHoist);
        if (storeToExisting) {
            builder_.createStore(closureVal, existingInfo->value);
            broadcastCaptureWrite(existingInfo, closureVal);
        }
        if (fdInBlock || !storeToExisting) {
            // Block-local binding (or a fresh top-level one).
            defineVariable(node->name, closureVal);
        }

        // Also store to module global for module-level function declarations
        if (isModuleGlobalVar(node->name)) {
            builder_.createStoreGlobal(modVarName(node->name), closureVal);
        }
    }
}


void ASTToHIR::visitArrowFunction(ast::ArrowFunction* node) {
    setSourceLine(node);
    // Generate unique function name for the arrow function
    std::string funcName = "__arrow_fn_" + std::to_string(arrowFuncCounter_++);

    // Create HIR function
    auto func = std::make_unique<HIRFunction>(funcName);
    func->isAsync = node->isAsync;
    func->isGenerator = false;  // Arrow functions can't be generators
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;

    // Set display name from assignment context (e.g., const myArrow = () => ...)
    if (!pendingClosureDisplayName_.empty()) {
        func->displayName = pendingClosureDisplayName_;
    }

    // Get function type info from inferred type if available
    std::shared_ptr<ts::FunctionType> tsFuncType = nullptr;
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Function) {
        tsFuncType = std::static_pointer_cast<ts::FunctionType>(node->inferredType);
    }

    // Add hidden __closure__ parameter as first parameter (for call_indirect compatibility)
    // call_indirect always passes the closure as the first argument
    func->params.push_back({"__closure__", HIRType::makePtr()});

    // Collect destructured parameter patterns for later extraction
    struct ArrowDestructuredParam {
        size_t paramIndex;
        ast::ObjectBindingPattern* objPattern = nullptr;
        ast::ArrayBindingPattern* arrPattern = nullptr;
        ast::Node* defaultInitializer = nullptr;
    };
    std::vector<ArrowDestructuredParam> arrowDestructuredParams;

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

        // If parameter has a default value, force Any type to receive undefined
        if (param->initializer) {
            paramType = HIRType::makeAny();
        }

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            arrowDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                param->initializer.get()});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            arrowDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
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
                func->firstNonSimpleParamIndex = i;
            }
        }

        func->params.push_back({paramName, paramType});
    }

    // Note: Arrow functions don't have their own 'arguments' object
    // (they inherit from enclosing scope), so no hidden __argN__ params needed.

    // Determine return type from inferred type or default to Any
    std::shared_ptr<HIRType> returnType = HIRType::makeAny();
    if (tsFuncType && tsFuncType->returnType) {
        returnType = convertType(tsFuncType->returnType);
    }
    func->returnType = returnType;

    // Save current context
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
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures
    auto savedInnerFuncClosures = std::move(innerFuncClosures_);
    innerFuncClosures_.clear();
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

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

    // Register parameters in the scope (with default value handling).
    preseedParamTDZ(func.get(), node->parameters);
    // Strategy B Phase 6c: per-parameter logic factored into bindOneParameter.
    // The slot-0 __closure__ has no AST parameter; user params start at index 1.
    for (size_t i = 0; i < func->params.size(); ++i) {
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/true);
    }

    // Emit destructuring extraction for parameters with binding patterns.
    // Strategy B Phase 6c: per-parameter logic factored into extractDestructuringForParam.
    for (auto& dp : arrowDestructuredParams) {
        extractDestructuringForParam(func.get(), dp.paramIndex,
            dp.objPattern, dp.arrPattern, dp.defaultInitializer);
    }

    // Async generators: end of PARAMETER prologue (see site in
    // visitFunctionDeclaration) — body throws after this reject next().
    if (func->isAsync && func->isGenerator) {
        builder_.createCall("ts_async_generator_body_started", {},
                            HIRType::makeVoid());
    } else if (func->isGenerator) {
        // Sync generator: eager-parameter model (marker = suspension 0 -> 1).
        builder_.createCall("ts_generator_body_started", {},
                            HIRType::makeVoid());
    }

    // Lower function body
    // The body can be either a BlockStatement or an Expression (implicit return)
    if (auto* blockStmt = dynamic_cast<ast::BlockStatement*>(node->body.get())) {
        // Pre-declare top-level `let`/`const` with the TDZ sentinel (mirrors
        // visitFunctionDeclaration; see visitFunctionExpression note).
        for (auto& stmt : blockStmt->statements) {
            if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
                auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
                if (!ident) continue;
                if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
                auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
                auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
                builder_.createStore(tdz, allocaVal, HIRType::makeAny());
                defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
                if (auto* vi = lookupVariableInfoInCurrentFunction(ident->name)) vi->isTDZ = true;
            }
        }

        // JavaScript function hoisting: pre-declare nested function names as variables
        // This allows functions to be called before they appear in source order.
        for (auto& stmt : blockStmt->statements) {
            if (auto* nestedFunc = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
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
                auto allocaVal = builder_.createAlloca(nestedFuncType, nestedFunc->name);
                builder_.createStore(builder_.createConstNull(), allocaVal);
                defineVariableAlloca(nestedFunc->name, allocaVal, nestedFuncType);
                // Function-name slot: Annex-B var-copy target for block fns.
                if (auto* vi_ = lookupVariableInfoInCurrentFunction(nestedFunc->name))
                    vi_->isFnHoist = true;
            }
        }

        // FIRST PASS: Process FunctionDeclarations to create closures (hoisting)
        for (auto& stmt : blockStmt->statements) {
            if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                lowerStatement(stmt.get());
            }
        }
        emitMutualRecursionFixup();

        // SECOND PASS: Process non-FunctionDeclaration statements in order
        for (auto& stmt : blockStmt->statements) {
            if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
                lowerStatement(stmt.get());
                if (builder_.isBlockTerminated()) {
                    break;
                }
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
    tryDepth_ = savedTryDepth_fn; withDepth_ = savedWithDepth_fn;
    withLexical_ = savedWithLexical_fn;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    innerFuncClosures_ = std::move(savedInnerFuncClosures);
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
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
        // so subsequent reads/writes in the outer function also use the cell.
        // When MULTIPLE function expressions / arrows capture the same outer
        // var (e.g., lodash's `upperFirst` referenced from camelCase, kebabCase,
        // snakeCase, startCase, upperCase callbacks), record each additional
        // closure in info->additionalCaptures so broadcastCaptureWrite reaches
        // every one when the var is later assigned.
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(lastValue_, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = captureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, captureIdx);
                    }
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
    setSourceLine(node);
    SPDLOG_DEBUG("[FE] ENTER: name={} scopes={} currentFunc={} bodySize={}",
        node->name.empty() ? "(anon)" : node->name,
        scopes_.size(),
        currentFunction_ ? currentFunction_->name : "null",
        node->body.size());
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
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;

    // Set display name: prefer node name, then assignment context
    if (!node->name.empty()) {
        func->displayName = node->name;
    } else if (!pendingClosureDisplayName_.empty()) {
        func->displayName = pendingClosureDisplayName_;
    }

    // Add hidden __closure__ parameter as first parameter (for call_indirect compatibility)
    // call_indirect always passes the closure as the first argument
    func->params.push_back({"__closure__", HIRType::makePtr()});

    // Collect destructured parameter patterns for later extraction.
    // (Mirrors the ArrowFunction path; the slot-0 __closure__ is already pushed,
    // so paramIndex == func->params.size() at collection time is the HIR index.)
    struct FEDestructuredParam {
        size_t paramIndex;
        ast::ObjectBindingPattern* objPattern = nullptr;
        ast::ArrayBindingPattern* arrPattern = nullptr;
        ast::Node* defaultInitializer = nullptr;
    };
    std::vector<FEDestructuredParam> feDestructuredParams;

    // Handle parameters
    size_t feParamIdx = 0;
    for (auto& param : node->parameters) {
        auto paramType = param->type.empty()
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        // If parameter has a default value, force Any type to receive undefined
        if (param->initializer) {
            paramType = HIRType::makeAny();
        }

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            feDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                param->initializer.get()});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            feDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                param->initializer.get()});
        } else {
            paramName = "param" + std::to_string(func->params.size());
        }

        // Rest parameter (...args): mirror the FunctionDeclaration path —
        // without this a function EXPRESSION's rest array never packed
        // (args read as undefined; exposed by Promise combinator tests that
        // patch Promise.resolve with a (...args) function).
        if (param->isRest) {
            func->hasRestParam = true;
            func->restParamIndex = func->params.size();
            if (paramType->kind != HIRTypeKind::Array) {
                paramType = HIRType::makeArray(paramType, false);
            }
        }

        // Track first non-simple param for ECMA-262 §10.2.5 fn.length.
        if (func->firstNonSimpleParamIndex == SIZE_MAX) {
            bool isDestructured =
                dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
            if (param->initializer || param->isRest || isDestructured) {
                func->firstNonSimpleParamIndex = feParamIdx;
            }
        }
        feParamIdx++;

        func->params.push_back({paramName, paramType});
    }

    // If the function body uses 'arguments', add hidden __argN__ params
    // so the padded calling convention args can be captured.
    {
        bool bodyUsesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                bodyUsesArguments = true;
                break;
            }
        }
        if (!bodyUsesArguments) bodyUsesArguments = paramsReferenceArguments(node->parameters);
        if (bodyUsesArguments) {
            while (func->params.size() < 10) {
                std::string argName = "__arg" + std::to_string(func->params.size() - 1) + "__";
                func->params.push_back({argName, HIRType::makeAny()});
            }
        }
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
    // Per-function "use strict" directive (ECMA-262 directive prologue):
    // strictCode_ drives the strict-mode with-write ReferenceError path
    // (SetMutableBinding re-validation). Program::isStrict only covers the
    // top level; a strict IIFE inside sloppy `with` needs this scan.
    bool savedStrict_fn = strictCode_;
    if (bodyHasUseStrictDirective(node->body)) strictCode_ = true;
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;  // Save outer function's pending captures
    auto savedInnerFuncClosures = std::move(innerFuncClosures_);
    innerFuncClosures_.clear();
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();  // Start fresh for this function

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope (marks function boundary for capture detection)
    pushFunctionScope(func.get());

    // Update function's value counter to start after parameters BEFORE the loop
    func->nextValueId = static_cast<uint32_t>(func->params.size());

    // Register parameters in the scope (with default value handling).
    preseedParamTDZ(func.get(), node->parameters);
    // Strategy B Phase 6b: per-parameter logic factored into bindOneParameter.
    // The slot-0 __closure__ has no AST parameter; user params start at index 1.
    for (size_t i = 0; i < func->params.size(); ++i) {
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/true);
    }

    // Emit destructuring extraction for parameters with binding patterns.
    // (FunctionExpression previously skipped this, so `{ m: function([a,b]){} }`
    // and object-method shorthand left the inner names bound to undefined.)
    for (auto& dp : feDestructuredParams) {
        extractDestructuringForParam(func.get(), dp.paramIndex,
            dp.objPattern, dp.arrPattern, dp.defaultInitializer);
    }

    // Async generators: end of PARAMETER prologue — body throws after this
    // reject the first next() promise (ts_agen_should_reject).
    if (func->isAsync && func->isGenerator) {
        builder_.createCall("ts_async_generator_body_started", {},
                            HIRType::makeVoid());
    } else if (func->isGenerator) {
        // Sync generator: eager-parameter model (marker = suspension 0 -> 1).
        builder_.createCall("ts_generator_body_started", {},
                            HIRType::makeVoid());
    }

    // If the function is named, make it available in its own scope (for recursion)
    // Alias the name to the __closure__ parameter (index 0) which represents
    // the function/closure pointer itself - ts_call_N handles dispatch correctly
    if (!node->name.empty()) {
        auto* closureInfo = lookupVariableInfo("__closure__");
        if (closureInfo && closureInfo->isAlloca) {
            defineVariableAlloca(node->name, closureInfo->value, closureInfo->elemType ? closureInfo->elemType : HIRType::makePtr());
        } else {
            // Fallback: reference parameter 0 directly
            auto closureParam = std::make_shared<HIRValue>(0, HIRType::makePtr(), node->name);
            defineVariable(node->name, closureParam);
        }
    }

    // Create 'arguments' array if the function body references 'arguments'.
    // Must be done at function entry before body lowering.
    {
        bool usesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) {
                usesArguments = true;
                break;
            }
        }
        if (!usesArguments) usesArguments = paramsReferenceArguments(node->parameters);
        if (usesArguments) {
            std::vector<std::shared_ptr<HIRValue>> callArgs;

            size_t userIdx = 0;
            for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
                if (func->params[i].first == "__closure__") continue;
                auto paramVal = lookupVariable(func->params[i].first);
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

    // JavaScript var hoisting for function expressions with nested func decls.
    // Pre-declare ALL `var` names in the function body — including those
    // nested in if/for/while/try blocks. ECMA-262 § 14.3.2: `var`
    // declarations hoist to the enclosing function scope, not the block
    // they appear in. Without recursive walking, code like
    //
    //   if (cond) { var x = 5; }
    //   ... x ...
    //
    // resolves the outer `x` to the surrounding scope (potentially a
    // wrong outer var or function) instead of the function-scoped local.
    // Lodash's createFind closure depends on this for `var iteratee`
    // declared inside an `if` branch.
    {
        std::vector<std::string> hoistedVars;
        std::vector<std::string> hoistedFns;
        for (auto& stmt : node->body) {
            collectHoistedVarNames(stmt.get(), hoistedVars, &hoistedFns);
        }
        for (auto& name : hoistedVars) {
            if (lookupVariableInfoInCurrentFunction(name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
            builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
            defineVariableAlloca(name, allocaVal, HIRType::makeAny());
            if (std::find(hoistedFns.begin(), hoistedFns.end(), name) != hoistedFns.end())
                if (auto* vi = lookupVariableInfoInCurrentFunction(name)) vi->isFnHoist = true;
        }
    }

    // Pre-declare top-level `let`/`const` with the TDZ sentinel so nested
    // FunctionDeclaration bodies (pass 1 below) can resolve outer-scope
    // captures — mirrors visitFunctionDeclaration. Without this, an IIFE
    // whose nested function declaration captures an outer `const` read it as
    // undefined; with a plain undefined pre-store the accidental-TDZ tests
    // regressed (the earlier revert) — the sentinel keeps both correct.
    for (auto& stmt : node->body) {
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
            if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
            auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
            if (!ident) continue;
            if (lookupVariableInfoInCurrentFunction(ident->name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
            auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
            builder_.createStore(tdz, allocaVal, HIRType::makeAny());
            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
            if (auto* vi = lookupVariableInfoInCurrentFunction(ident->name)) vi->isTDZ = true;
        }
    }

    // JavaScript function hoisting: pre-declare nested function names as variables.
    // This allows functions to be called before they appear in source order.
    for (auto& stmt : node->body) {
        if (auto* funcDecl = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
            for (auto& param : funcDecl->parameters) {
                std::shared_ptr<HIRType> paramType = HIRType::makeAny();
                if (!param->type.empty()) {
                    paramType = convertTypeFromString(param->type);
                }
                funcType->paramTypes.push_back(paramType);
            }
            funcType->returnType = funcDecl->returnType.empty()
                ? HIRType::makeAny()
                : convertTypeFromString(funcDecl->returnType);

            // Use existing alloca from var hoisting if available, else create new
            auto* existing = lookupVariableInfoInCurrentFunction(funcDecl->name);
            if (existing && existing->isAlloca) {
                // Update type to function type
                existing->elemType = funcType;
            } else {
                auto allocaVal = builder_.createAlloca(funcType, funcDecl->name);
                builder_.createStore(builder_.createConstNull(), allocaVal);
                defineVariableAlloca(funcDecl->name, allocaVal, funcType);
                // Function-name slot: Annex-B var-copy target for block fns.
                if (auto* vi_ = lookupVariableInfoInCurrentFunction(funcDecl->name))
                    vi_->isFnHoist = true;
            }
        }
    }

    // Lower function body in two passes for proper JavaScript function hoisting:
    // FIRST PASS: Process FunctionDeclarations to create closures
    for (auto& stmt : node->body) {
        if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
        }
    }
    emitMutualRecursionFixup();

    // SECOND PASS: Process non-FunctionDeclaration statements in order
    for (auto& stmt : node->body) {
        if (!dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
            lowerStatement(stmt.get());
            if (builder_.isBlockTerminated()) {
                break;
            }
        }
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
    tryDepth_ = savedTryDepth_fn; withDepth_ = savedWithDepth_fn;
    withLexical_ = savedWithLexical_fn;
    strictCode_ = savedStrict_fn;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;  // Restore outer function's pending captures
    innerFuncClosures_ = std::move(savedInnerFuncClosures);
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
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
        // so subsequent reads/writes in the outer function also use the cell.
        // See visitArrowFunction for the multi-capturer rationale.
        int captureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t scopeIndex = 0;
            if (!isCapturedVariable(capName, &scopeIndex)) {
                // Variable is in this function's scope, mark it as captured
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(), capName + "$closure");
                    builder_.createStore(lastValue_, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = captureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, captureIdx);
                    }
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
    func->sourceLine = node->line;
    func->sourceFile = node->sourceFile;
    // Method .name = the property key (ECMA-262 SetFunctionName); accessors are
    // prefixed "get "/"set ". Without this an object/class method's funcName is
    // the mangled "__method_x_N" and .name leaks the mangled form.
    if (!methodName.empty()) {
        func->displayName = node->isGetter ? ("get " + methodName)
                          : node->isSetter ? ("set " + methodName)
                          : methodName;
    }

    // Add implicit 'this' parameter for methods
    func->params.push_back({"this", HIRType::makeAny()});

    // Handle explicit parameters
    // For getters/setters, force params to be Any (TsValue*) since they will be called
    // through the runtime's dynamic dispatch which passes TsValue* arguments
    bool forceAnyParams = node->isGetter || node->isSetter;
    size_t mdParamIdx = 0;
    // Collect destructured parameter patterns for later extraction. (Object-literal
    // method shorthand routes here; without this, `{ m([a,b]) {} }` left a/b bound
    // to undefined. Class methods use a separate path that already extracts.)
    struct MDDestructuredParam {
        size_t paramIndex;
        ast::ObjectBindingPattern* objPattern = nullptr;
        ast::ArrayBindingPattern* arrPattern = nullptr;
        ast::Node* defaultInitializer = nullptr;
    };
    std::vector<MDDestructuredParam> mdDestructuredParams;
    for (auto& param : node->parameters) {
        auto paramType = (forceAnyParams || param->type.empty())
            ? HIRType::makeAny()
            : convertTypeFromString(param->type);

        std::string paramName;
        if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
            paramName = ident->name;
        } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            mdDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                param->initializer.get()});
        } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
            paramName = "param" + std::to_string(func->params.size());
            paramType = HIRType::makeAny();
            mdDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
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
                func->firstNonSimpleParamIndex = mdParamIdx;
            }
        }
        mdParamIdx++;

        func->params.push_back({paramName, paramType});
    }

    // If the method body uses `arguments`, pad with hidden __argN__ params so
    // extra call args physically reach ts_create_arguments_from_params.
    // Object-literal (generator) methods route here; without the pad,
    // arguments.length was right (ts_last_call_argc) but arguments[N] read
    // undefined.
    {
        bool mdBodyUsesArgs = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) { mdBodyUsesArgs = true; break; }
        }
        if (!mdBodyUsesArgs) mdBodyUsesArgs = paramsReferenceArguments(node->parameters);
        if (mdBodyUsesArgs) {
            while (func->params.size() < 10) {
                std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                func->params.push_back({argName, HIRType::makeAny()});
            }
        }
    }

    // Determine return type - always Any for method definitions since they are called
    // through dynamic dispatch (ts_call_N) which expects ptr (NaN-boxed TsValue*) returns
    func->returnType = HIRType::makeAny();

    // Save current context
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
    HIRBlock* savedBlock = currentBlock_;
    auto savedCaptures = pendingCaptures_;
    // Save loop/switch/label stacks - nested functions must not see parent's break/continue targets
    auto savedLoopStack = loopStack_;
    auto savedSwitchStack = switchStack_;
    auto savedLabeledLoops = labeledLoops_;
    loopStack_ = {};
    switchStack_ = {};
    labeledLoops_ = {};

    currentFunction_ = func.get();
    clearPendingCaptures();

    // Create entry block
    auto entryBlock = func->createBlock("entry");
    builder_.setInsertPoint(entryBlock);
    currentBlock_ = entryBlock;

    // Enter function scope
    pushFunctionScope(func.get());

    // Register parameters in the scope (including 'this').
    // Strategy B Phase 6d: per-parameter logic factored into bindOneParameter.
    // Methods use defineVariable (not defineVariableAlloca) — params are not
    // reassignable. Slot 0 is 'this' (synthetic, no AST param); user params
    // start at index 1.
    // Set nextValueId BEFORE the bind loop (mirroring the arrow/funcexpr paths)
    // so values created while applying parameter defaults / destructuring don't
    // collide with the SSA IDs reserved for params 0..N.
    func->nextValueId = static_cast<uint32_t>(func->params.size());
    preseedParamTDZ(func.get(), node->parameters);
    for (size_t i = 0; i < func->params.size(); ++i) {
        size_t astParamIdx = (i >= 1) ? (i - 1) : SIZE_MAX;
        ast::Parameter* astParam = (astParamIdx < node->parameters.size())
            ? node->parameters[astParamIdx].get() : nullptr;
        bindOneParameter(func.get(), i, astParam, /*useAlloca=*/false);
    }

    // Emit destructuring extraction for parameters with binding patterns.
    for (auto& dp : mdDestructuredParams) {
        extractDestructuringForParam(func.get(), dp.paramIndex,
            dp.objPattern, dp.arrPattern, dp.defaultInitializer);
    }

    // Create the 'arguments' object if the method body references it (object-
    // literal methods, getters/setters, etc. lowered via this path lacked it).
    // In the eager param prologue so generator/async methods capture call args.
    {
        bool usesArguments = false;
        for (auto& stmt : node->body) {
            if (containsArgumentsIdentifier(stmt.get())) { usesArguments = true; break; }
        }
        if (!usesArguments) usesArguments = paramsReferenceArguments(node->parameters);
        if (usesArguments) {
            std::vector<std::shared_ptr<HIRValue>> callArgs;
            size_t userIdx = 0;
            for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
                // `arguments` holds JS args only — exclude the receiver `this`
                // (slot 0 for object-literal methods/getters/setters) and the
                // synthetic closure param.
                if (func->params[i].first == "__closure__" ||
                    func->params[i].first == "this") continue;
                auto paramVal = lookupVariable(func->params[i].first);
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

    // Async generators: end of PARAMETER prologue — body throws after this
    // reject the first next() promise (ts_agen_should_reject).
    if (func->isAsync && func->isGenerator) {
        builder_.createCall("ts_async_generator_body_started", {},
                            HIRType::makeVoid());
    } else if (func->isGenerator) {
        // Sync generator: eager-parameter model (marker = suspension 0 -> 1).
        builder_.createCall("ts_generator_body_started", {},
                            HIRType::makeVoid());
    }

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
    tryDepth_ = savedTryDepth_fn; withDepth_ = savedWithDepth_fn;
    withLexical_ = savedWithLexical_fn;
    currentBlock_ = savedBlock;
    pendingCaptures_ = savedCaptures;
    loopStack_ = savedLoopStack;
    switchStack_ = savedSwitchStack;
    labeledLoops_ = savedLabeledLoops;
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
        auto methodClosure = builder_.createMakeClosure(funcName, captureValues, closureFuncType);

        // Redirect the enclosing scope's reads/writes of each captured variable
        // through this method's closure cell, so a mutation inside the method
        // (e.g. `var d=0; ({ m(){ d++; } }).m()`) propagates back to the outer
        // `d`. Mirrors the FunctionDeclaration path (see ~line 3096). Without
        // this the outer variable stays bound to its stale alloca and the
        // method's writes are invisible — the dominant class/object-method
        // closure-capture bug behind the test262 "callCount" cluster.
        int mdCaptureIdx = 0;
        for (const auto& cap : innerCaptures) {
            const std::string& capName = cap.first;
            size_t sIdx = 0;
            if (!isCapturedVariable(capName, &sIdx)) {
                auto* info = lookupVariableInfo(capName);
                if (info) {
                    auto closureAlloca = builder_.createAlloca(HIRType::makeAny(),
                                                               capName + "$mclosure");
                    builder_.createStore(methodClosure, closureAlloca);
                    if (!info->isCapturedByNested) {
                        info->isCapturedByNested = true;
                        info->closurePtr = closureAlloca;
                        info->captureIndex = mdCaptureIdx;
                    } else {
                        info->additionalCaptures.emplace_back(closureAlloca, mdCaptureIdx);
                    }
                }
            }
            mdCaptureIdx++;
        }
        return methodClosure;
    } else {
        // Pass the function type so SetPropStatic knows to box it as a function
        return builder_.createLoadFunction(funcName, closureFuncType);
    }
}

}  // namespace ts::hir
