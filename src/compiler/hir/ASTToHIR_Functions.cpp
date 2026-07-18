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
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering state
    // (try/with depth, captures, closure lists, loop/label/break stacks);
    // reset() at the old restore point returns to the OUTER context.
    std::optional<FunctionLoweringScope> fnScope{std::in_place, *this};
    HIRBlock* savedBlock = currentBlock_;

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

    // Register parameters in the scope so they can be looked up.
    // Parameter values have IDs 0, 1, 2, ... matching their index in HIRToLLVM.
    preseedParamTDZ(func.get(), node->parameters);
    // Direct eval in a param default of a non-arrow function always crosses an
    // 'arguments' binding on the lexEnv->varEnv walk (bit1).
    int savedPECF = paramEvalCtxFlags_;
    paramEvalCtxFlags_ = 1 | 2;
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
    paramEvalCtxFlags_ = savedPECF;

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

    // A function lexically inside a `with` restores its captured object
    // environment for the duration of the call (ES 14.11 — the closure's
    // scope chain keeps the with env even after the with statement exits).
    // Generators/async excluded: their bodies suspend without the exit.
    if (withLexical_ && !func->isGenerator && !func->isAsync) {
        builder_.createCall("ts_with_enter_fn",
            {builder_.createConstString(funcName)}, HIRType::makeVoid());
        withEnvEntered_ = true;
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
        if (withEnvEntered_)
            builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
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
    fnScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
    currentBlock_ = savedBlock;
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

        // Transitive-capture annotation: a slot whose variable is ITSELF an
        // outer capture of the current function must alias the parent's cell
        // (not copy the value) so mutations stay shared across every level.
        std::vector<std::string> capFromParent;
        for (const auto& cap : innerCaptures) {
            size_t cfpIdx = 0;
            capFromParent.push_back(
                isCapturedVariable(cap.first, &cfpIdx) ? cap.first : std::string());
        }
        auto closureVal = builder_.createMakeClosure(funcName, captureValues, closureFuncType, &capFromParent);
        finishClosure(funcName, closureVal, innerCaptures);

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
        // Annex B B.3.3.1 step 3.a.ii: the var-copy targets the FUNCTION-SCOPE
        // hoist slot even when a SIMPLE catch parameter shadows the name at
        // the declaration site (B.3.5 — the no-skip-try family). ONLY such
        // params are transparent: any other intervening binding (an outer
        // block's own `function f` lexical, a let/const) means promotion was
        // suppressed and NO var-copy happens (nested-blocks-with-fun-decl).
        if (fdInBlock && (!existingInfo || !existingInfo->isFnHoist)) {
            VariableInfo* hoist = nullptr;
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                if (it->isFunctionBoundary && it->owningFunction &&
                    it->owningFunction != currentFunction_) break;
                auto found = it->variables.find(node->name);
                if (found != it->variables.end()) {
                    if (found->second.isFnHoist) { hoist = &found->second; break; }
                    if (!found->second.isSimpleCatchParam) break;
                    // simple catch param: transparent, keep walking outward
                }
                if (it->isFunctionBoundary) break;
            }
            if (hoist) existingInfo = hoist;
        }
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
        // NOT for a block-level decl whose Annex-B promotion was SUPPRESSED
        // (no hoist slot — e.g. toplevel `let f` collision): the write
        // clobbered the lexical module binding with a closure pointer that
        // later reads decoded as a garbage double (skip-early-err family).
        if (isModuleGlobalVar(node->name) && (!fdInBlock || storeToExisting)) {
            builder_.createStoreGlobal(modVarName(node->name), closureVal);
        }
        // AnnexB B.3.3.2 var-copy in GLOBAL code also updates the global
        // object own property (declared at hoist).
        if (fdInBlock && storeToExisting &&
            module_->annexBGlobalFnDecls.count(node->name)) {
            auto nameStr = builder_.createConstString(node->name);
            builder_.createCall("ts_global_bind_fn", {nameStr, closureVal},
                                HIRType::makeVoid());
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
        finishClosure(funcName, closureVal, {});

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
        // Annex B B.3.3.1 step 3.a.ii: target the FUNCTION-SCOPE hoist slot
        // past a SIMPLE catch parameter only (B.3.5; see the captures variant
        // above — any other intervening binding suppresses the var-copy).
        if (fdInBlock && (!existingInfo || !existingInfo->isFnHoist)) {
            VariableInfo* hoist = nullptr;
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                if (it->isFunctionBoundary && it->owningFunction &&
                    it->owningFunction != currentFunction_) break;
                auto found = it->variables.find(node->name);
                if (found != it->variables.end()) {
                    if (found->second.isFnHoist) { hoist = &found->second; break; }
                    if (!found->second.isSimpleCatchParam) break;
                }
                if (it->isFunctionBoundary) break;
            }
            if (hoist) existingInfo = hoist;
        }
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

        // Also store to module global for module-level function declarations.
        // Suppressed-promotion block decls skip the write (see captures arm).
        if (isModuleGlobalVar(node->name) && (!fdInBlock || storeToExisting)) {
            builder_.createStoreGlobal(modVarName(node->name), closureVal);
        }
        // AnnexB B.3.3.2 var-copy in GLOBAL code also updates the global
        // object own property (declared at hoist).
        if (fdInBlock && storeToExisting &&
            module_->annexBGlobalFnDecls.count(node->name)) {
            auto nameStr = builder_.createConstString(node->name);
            builder_.createCall("ts_global_bind_fn", {nameStr, closureVal},
                                HIRType::makeVoid());
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
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering state.
    std::optional<FunctionLoweringScope> fnScope{std::in_place, *this};
    HIRBlock* savedBlock = currentBlock_;

    currentFunction_ = func.get();
    // Field-init eval context (flags bit3) propagates through ARROWS —
    // they do not rebind 'arguments' (nested-direct-eval-err-contains-
    // arguments). Regular functions rebind it and drop the context via
    // the owner check. Restored with savedEvalOwner_arrow below.
    HIRFunction* savedEvalOwner_arrow = evalFlagsOwner_;
    if ((activeEvalFlags_ & 8) && evalFlagsOwner_ == savedFunc)
        evalFlagsOwner_ = func.get();

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
    // Arrows have no own 'arguments'; a param-default direct eval only crosses
    // an 'arguments' binding when a parameter is literally named 'arguments'.
    int savedPECF = paramEvalCtxFlags_;
    {
        bool argsParam = false;
        for (auto& pr : func->params)
            if (pr.first == "arguments") { argsParam = true; break; }
        paramEvalCtxFlags_ = 1 | (argsParam ? 2 : 0);
    }
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
    paramEvalCtxFlags_ = savedPECF;

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

    // Arrow lexically inside a `with`: restore the captured object env for
    // the call (see visitFunctionDeclaration site).
    if (withLexical_ && !func->isGenerator && !func->isAsync) {
        builder_.createCall("ts_with_enter_fn",
            {builder_.createConstString(funcName)}, HIRType::makeVoid());
        withEnvEntered_ = true;
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

        // ECMA-262 §14.3.2 / §15.3.4: hoist every `var` declaration in the arrow
        // body (including those nested in if/else/loops/try/switch) to the
        // function environment, pre-initialized to undefined. Arrow functions
        // LACKED this block (only function declarations/expressions had it), so a
        // `var v` read BEFORE its declaration line resolved to an outer/global
        // binding (or threw ReferenceError) instead of the hoisted local
        // undefined — exposed by Symbol.unscopables-with tests, but affecting any
        // arrow that reads a var before its declaration. Mirrors the block in
        // visitFunctionDeclaration (ASTToHIR.cpp).
        {
            std::vector<std::string> hoistedVars;
            std::vector<std::string> hoistedFns;
            for (auto& stmt : blockStmt->statements) {
                collectHoistedVarNames(stmt.get(), hoistedVars, &hoistedFns);
            }
            for (auto& name : hoistedVars) {
                if (lookupVariableInfoInCurrentFunction(name)) continue;
                auto allocaVal = builder_.createAlloca(HIRType::makeAny(), name);
                builder_.createStore(builder_.createConstUndefined(), allocaVal, HIRType::makeAny());
                defineVariableAlloca(name, allocaVal, HIRType::makeAny());
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
            if (withEnvEntered_)
                builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
            builder_.createReturn(retVal);
        }
    }

    // Add implicit return void if no terminator
    if (!hasTerminator()) {
        if (withEnvEntered_)
            builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
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
    evalFlagsOwner_ = savedEvalOwner_arrow;
    fnScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
    currentBlock_ = savedBlock;
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
        // Transitive-capture annotation: a slot whose variable is ITSELF an
        // outer capture of the current function must alias the parent's cell
        // (not copy the value) so mutations stay shared across every level.
        std::vector<std::string> capFromParent;
        for (const auto& cap : innerCaptures) {
            size_t cfpIdx = 0;
            capFromParent.push_back(
                isCapturedVariable(cap.first, &cfpIdx) ? cap.first : std::string());
        }
        lastValue_ = builder_.createMakeClosure(funcName, captureValues, closureFuncType, &capFromParent);
        finishClosure(funcName, lastValue_, innerCaptures);
    } else {
        // No captures, but still wrap in a closure for consistency with call_indirect
        // which always expects a TsClosure* (not a raw function pointer)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        lastValue_ = builder_.createMakeClosure(funcName, emptyCaptureValues, closureFuncType);
        finishClosure(funcName, lastValue_, {});
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
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering state.
    std::optional<FunctionLoweringScope> fnScope{std::in_place, *this};
    // Per-function "use strict" directive (ECMA-262 directive prologue):
    // strictCode_ drives the strict-mode with-write ReferenceError path
    // (SetMutableBinding re-validation). Program::isStrict only covers the
    // top level; a strict IIFE inside sloppy `with` needs this scan.
    // (Saved by fnScope; strictness otherwise inherits.)
    if (bodyHasUseStrictDirective(node->body)) strictCode_ = true;
    HIRBlock* savedBlock = currentBlock_;

    currentFunction_ = func.get();

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
    // Non-arrow: a param-default direct eval always crosses an 'arguments'
    // binding (bit1).
    int savedPECF = paramEvalCtxFlags_;
    paramEvalCtxFlags_ = 1 | 2;
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
    paramEvalCtxFlags_ = savedPECF;

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

    // A function lexically inside a `with` restores its captured object
    // environment for the duration of the call (ES 14.11 — the closure's
    // scope chain keeps the with env even after the with statement exits;
    // Sputnik S12.10_A1.12: f defined in with, called after). Generators/
    // async are excluded: their bodies suspend without running the exit.
    if (withLexical_ && !func->isGenerator && !func->isAsync) {
        builder_.createCall("ts_with_enter_fn",
            {builder_.createConstString(funcName)}, HIRType::makeVoid());
        withEnvEntered_ = true;
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
        {
            // Annex B B.3.3: suppress the var-copy for a block-level fn name that
            // clashes with a top-level lexical declaration of THIS function
            // (skip-early-err). Mirrors visitFunctionDeclaration — without it the
            // fn-hoist alloca is created here BEFORE the let/const pre-pass runs,
            // so the lexical binding never gets its own slot and the block-level
            // function's Annex B var-copy corrupts the `let`/`const` value.
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
        if (withEnvEntered_)
            builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
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
    fnScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
    currentBlock_ = savedBlock;
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
        // Transitive-capture annotation: a slot whose variable is ITSELF an
        // outer capture of the current function must alias the parent's cell
        // (not copy the value) so mutations stay shared across every level.
        std::vector<std::string> capFromParent;
        for (const auto& cap : innerCaptures) {
            size_t cfpIdx = 0;
            capFromParent.push_back(
                isCapturedVariable(cap.first, &cfpIdx) ? cap.first : std::string());
        }
        lastValue_ = builder_.createMakeClosure(funcName, captureValues, closureFuncType, &capFromParent);
        finishClosure(funcName, lastValue_, innerCaptures);
    } else {
        // No captures, but still wrap in a closure for consistency with call_indirect
        // which always expects a TsClosure* (not a raw function pointer)
        std::vector<std::shared_ptr<HIRValue>> emptyCaptureValues;
        lastValue_ = builder_.createMakeClosure(funcName, emptyCaptureValues, closureFuncType);
        finishClosure(funcName, lastValue_, {});
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
        // A TypeScript `this` parameter is type-only: the implicit `this`
        // formal was already pushed above; keeping the explicit one would
        // shift every real parameter by one slot (getter read garbage,
        // setter crashed at runtime — tsconf thisType cluster).
        if (param->isThisParameter) continue;
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
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering state.
    // This site previously MISSED innerFuncClosures_ save/restore — a
    // method body defining nested function-expression closures leaked or
    // lost the parent's list (latent divergence #1 from the audit).
    std::optional<FunctionLoweringScope> fnScope{std::in_place, *this};
    HIRBlock* savedBlock = currentBlock_;

    currentFunction_ = func.get();

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
    // Non-arrow (method): a param-default direct eval always crosses an
    // 'arguments' binding (bit1).
    int savedPECF = paramEvalCtxFlags_;
    paramEvalCtxFlags_ = 1 | 2;
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
    paramEvalCtxFlags_ = savedPECF;

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
        if (withEnvEntered_)
            builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
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
    fnScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
    currentBlock_ = savedBlock;
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
        // Transitive-capture annotation: a slot whose variable is ITSELF an
        // outer capture of the current function must alias the parent's cell
        // (not copy the value) so mutations stay shared across every level.
        std::vector<std::string> capFromParent;
        for (const auto& cap : innerCaptures) {
            size_t cfpIdx = 0;
            capFromParent.push_back(
                isCapturedVariable(cap.first, &cfpIdx) ? cap.first : std::string());
        }
        auto methodClosure = builder_.createMakeClosure(funcName, captureValues, closureFuncType, &capFromParent);

        // Redirect the enclosing scope's reads/writes of each captured variable
        // through this method's closure cell, so a mutation inside the method
        // (e.g. `var d=0; ({ m(){ d++; } }).m()`) propagates back to the outer
        // `d`. Mirrors the FunctionDeclaration path (see ~line 3096). Without
        // this the outer variable stays bound to its stale alloca and the
        // method's writes are invisible — the dominant class/object-method
        // closure-capture bug behind the test262 "callCount" cluster.
        // Methods are never `with`-scope-bound (emitWithBind=false — the
        // one intentional divergence from the other MakeClosure trailers).
        finishClosure(funcName, methodClosure, innerCaptures,
                      /*emitWithBind=*/false);
        return methodClosure;
    } else {
        // Pass the function type so SetPropStatic knows to box it as a function
        return builder_.createLoadFunction(funcName, closureFuncType);
    }
}

}  // namespace ts::hir
