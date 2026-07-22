#include "ASTToHIR_Internal.h"

namespace ts::hir {


void ASTToHIR::visitExpressionStatement(ast::ExpressionStatement* node) {
    setSourceLine(node);
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitBlockStatement(ast::BlockStatement* node) {
    setSourceLine(node);
    // `with (head) body` desugars to a synthetic block carrying the head:
    // push ToObject(head) on the runtime with-scope stack; the identifier
    // resolver consults it (ES 14.11 object Environment Record). Popped on
    // normal fall-through here and by return/break/continue via
    // ts_with_pop_n (mirrors tryDepth_/PopHandler unwinding).
    bool isWith = node->withHead != nullptr;
    if (isWith) {
        auto headVal = lowerExpression(node->withHead.get());
        builder_.createCall("ts_with_push", {boxValueIfNeeded(headVal)},
                            HIRType::makeVoid());
        withDepth_++;
    }
    // Synthetic blocks (from multi-var declarations like "var a = 1, b = 2;")
    // should NOT create a new scope - variables need to be visible in the
    // enclosing scope, just like individual var declarations would be.
    if (!node->isSynthetic) {
        pushScope();
        // Block-level let/const TDZ pre-seed (ES 14.2.3): a read (including
        // typeof) before the declaration initializes throws ReferenceError.
        // Mirrors the function-toplevel seed; a shadowing block let gets its
        // own sentinel slot (guard on THIS scope only, not the function).
        for (auto& stmt : node->statements) {
            auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
            if (!vd || (vd->varKind != ast::VarKind::Let &&
                        vd->varKind != ast::VarKind::Const)) continue;
            auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
            if (!ident) continue;
            if (scopes_.back().variables.count(ident->name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
            auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
            builder_.createStore(tdz, allocaVal, HIRType::makeAny());
            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
            if (!scopes_.empty()) {
                auto it = scopes_.back().variables.find(ident->name);
                if (it != scopes_.back().variables.end()) it->second.isTDZ = true;
            }
        }
    }
    for (auto& stmt : node->statements) {
        lowerStatement(stmt.get());
        // Stop processing statements after a terminator (return, throw, etc.)
        // This prevents dead code from being emitted after control flow ends
        if (builder_.isBlockTerminated()) {
            break;
        }
    }
    if (!node->isSynthetic) {
        popScope();
    }
    if (isWith) {
        withDepth_--;
        if (!builder_.isBlockTerminated()) {
            builder_.createCall("ts_with_pop", {}, HIRType::makeVoid());
        }
    }
}

void ASTToHIR::visitReturnStatement(ast::ReturnStatement* node) {
    setSourceLine(node);
    // Pop all active exception handlers before returning from inside try blocks.
    // Without this, a tail-call return destroys the stack frame but leaves the
    // handler on exceptionStack, creating a "zombie frame" that longjmp can
    // jump back to — causing stack corruption and crashes.
    // IMPORTANT: evaluate the return expression FIRST (while handler is still
    // active), then pop handlers. This ensures try/catch still protects the
    // expression evaluation (e.g., `return parseUrl(req).pathname` must be
    // caught if parseUrl throws).
    std::shared_ptr<HIRValue> retVal;
    if (node->expression) {
        retVal = lowerExpression(node->expression.get());
        // "use fast" struct value semantics: returning a struct read from an
        // lvalue yields an independent copy to the caller.
        retVal = maybeCloneStruct(retVal, node->expression.get());
    }

    // ES 14.7.5.7 step 6.k: a return statement leaving one or more for-of
    // loops closes their iterators (innermost first) — after the return
    // expression is evaluated, before the completion propagates.
    if (!forofIterStack_.empty()) {
        emitOpenIteratorCloses(0);
    }

    // ES 14.15: a `return` inside one or more enclosing try-with-finally blocks
    // must run every enclosing finally (in order) BEFORE returning. Route the
    // return completion through those finallys; the outermost finally's
    // epilogue performs the actual return (or a finally's own abrupt completion
    // overrides it). crossFinallyDepth = 0: a return crosses ALL finallys.
    if (!finallyStack_.empty()) {
        std::shared_ptr<HIRValue> boxed = retVal ? boxValueIfNeeded(retVal)
                                                 : builder_.createConstUndefined();
        routeAbruptThroughFinally(/*isReturn=*/true, /*target=*/nullptr,
                                  /*targetTryDepth=*/0, /*targetWithDepth=*/0,
                                  /*crossFinallyDepth=*/0, boxed);
        return;
    }

    if (node->expression) {
        for (int i = 0; i < tryDepth_; i++) {
            builder_.createPopHandler();
        }
        if (withDepth_ > 0) {
            builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(withDepth_)}, HIRType::makeVoid());
        }
        if (withEnvEntered_)
            builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
        builder_.createReturn(retVal);
    } else {
        for (int i = 0; i < tryDepth_; i++) {
            builder_.createPopHandler();
        }
        if (withDepth_ > 0) {
            builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(withDepth_)}, HIRType::makeVoid());
        }
        if (withEnvEntered_)
            builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
        builder_.createReturnVoid();
    }
}

void ASTToHIR::visitIfStatement(ast::IfStatement* node) {
    setSourceLine(node);
    auto cond = lowerExpression(node->condition.get());

    auto* thenBlock = createBlock("if.then");
    auto* elseBlock = createBlock("if.else");
    auto* mergeBlock = createBlock("if.end");

    builder_.createCondBranch(cond, thenBlock, elseBlock);

    // Annex B.3.2: a bare FunctionDeclaration in if-body position is implicitly
    // wrapped in a Block, giving it block-level lexical scope. The parser wraps
    // it only in a parser-side scope (not an AST Block), so lower it under a
    // pushed HIR scope here — otherwise it is misread as function-scoped
    // (fdInBlock=false) and its Annex B var-copy overwrites a same-named
    // parameter/lexical binding that should have suppressed it (skip-param /
    // skip-dft-param).
    bool thenIsFn = dynamic_cast<ast::FunctionDeclaration*>(node->thenStatement.get()) != nullptr;

    // Then block
    builder_.setInsertPoint(thenBlock);
    currentBlock_ = thenBlock;
    if (thenIsFn) pushScope();
    lowerStatement(node->thenStatement.get());
    if (thenIsFn) popScope();
    emitBranchIfNeeded(mergeBlock);

    // Else block
    builder_.setInsertPoint(elseBlock);
    currentBlock_ = elseBlock;
    if (node->elseStatement) {
        bool elseIsFn = dynamic_cast<ast::FunctionDeclaration*>(node->elseStatement.get()) != nullptr;
        if (elseIsFn) pushScope();
        lowerStatement(node->elseStatement.get());
        if (elseIsFn) popScope();
    }
    emitBranchIfNeeded(mergeBlock);

    // Continue in merge block
    builder_.setInsertPoint(mergeBlock);
    currentBlock_ = mergeBlock;
}

void ASTToHIR::visitWhileStatement(ast::WhileStatement* node) {
    setSourceLine(node);
    auto* condBlock = createBlock("while.cond");
    auto* bodyBlock = createBlock("while.body");
    auto* endBlock = createBlock("while.end");

    // Push loop context for break/continue
    LoopContext ctx = {condBlock, endBlock, withDepth_, tryDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);
    breakTargetMeta_.push({tryDepth_, withDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()});

    // Register with label if this loop is labeled
    std::string myLabel;
    if (!pendingLabel_.empty()) {
        myLabel = pendingLabel_;
        labeledLoops_[myLabel] = ctx;
        pendingLabel_.clear();  // Clear so nested loops don't also register
    }

    // For do-while, jump to body first (body executes before condition).
    // For while, jump to condition first.
    builder_.createBranch(node->isDoWhile ? bodyBlock : condBlock);

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
    breakTargetStack_.pop();
    breakTargetMeta_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForStatement(ast::ForStatement* node) {
    setSourceLine(node);
    auto* condBlock = createBlock("for.cond");
    auto* bodyBlock = createBlock("for.body");
    auto* updateBlock = createBlock("for.update");
    auto* endBlock = createBlock("for.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock, withDepth_, tryDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);
    breakTargetMeta_.push({tryDepth_, withDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()});

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

    // ECMA-262 14.7.4.4 CreatePerIterationEnvironment: each iteration of
    // `for (let i ...)` should create a fresh binding so closures created in
    // the body snapshot THIS iter's value. Until we have full by-reference
    // cells, simulate the per-iter semantics for vars declared in the for's
    // init scope: clear closurePtr before the update step. This makes the
    // update read/write the alloca directly (via the existing null-closure
    // fallback) and leaves the captured closure's cell holding its body-time
    // snapshot. Next iter's cond also reads via the cleared alloca, then the
    // body re-creates a closure (new cell) for the new iter.
    if (!scopes_.empty()) {
        for (auto& kv : scopes_.back().variables) {
            auto& info = kv.second;
            if (info.isCapturedByNested && info.closurePtr) {
                // Raw C++ nullptr — not NaN-boxed null (0x02). The
                // LoadCaptureFromClosure runtime check tests for raw 0x0 to
                // trigger the alloca fallback path.
                auto nullVal = builder_.createConstRawNullPtr();
                builder_.createStore(nullVal, info.closurePtr);
            }
        }
    }

    if (node->incrementor) {
        lowerExpression(node->incrementor.get());
    }
    builder_.createBranch(condBlock);

    loopStack_.pop();
    breakTargetStack_.pop();
    breakTargetMeta_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

// One IteratorClose (ES 7.4.6 with a NON-throw completion). Sync loops call
// the strict runtime close (GetMethod errors + return()-call errors propagate,
// non-object result is a TypeError). for-await-of loops perform ES 7.4.7
// AsyncIteratorClose: the return() call happens in the runtime helper, its
// result is AWAITED here in lowered async code, then validated.
void ASTToHIR::emitCloseOneIterator(std::shared_ptr<HIRValue> iterVal,
                                    bool isAwait) {
    if (!isAwait) {
        builder_.createCall("ts_iterator_close_strict", {iterVal},
                            HIRType::makeVoid());
        return;
    }
    auto res = builder_.createCall("ts_iterator_close_get_result", {iterVal},
                                   HIRType::makeAny());
    // NULL result: the iterator has no return method — nothing to await.
    auto isNull = builder_.createCmpEqPtr(res, builder_.createConstNull());
    auto* awaitBB = createBlock("forof.close.await");
    auto* doneBB = createBlock("forof.close.done");
    builder_.createCondBranch(isNull, doneBB, awaitBB);
    builder_.setInsertPoint(awaitBB);
    currentBlock_ = awaitBB;
    auto awaited = builder_.createAwait(res);
    builder_.createCall("ts_iterator_close_validate", {awaited},
                        HIRType::makeVoid());
    builder_.createBranch(doneBB);
    builder_.setInsertPoint(doneBB);
    currentBlock_ = doneBB;
}

// Close every open for-of iterator in [fromDepth..end), innermost first —
// used when a labeled break/continue or a return statement crosses one or
// more for-of loops (ES 14.7.5.7 step 6.k: LoopContinues false ->
// IteratorClose). Iterators separated from the abrupt statement by an
// enclosing finally are skipped (their close/finally ordering interleaves;
// banked — the finally dispatch machinery does not model iterators).
void ASTToHIR::emitOpenIteratorCloses(int fromDepth) {
    for (int i = (int)forofIterStack_.size() - 1; i >= fromDepth; i--) {
        const auto& e = forofIterStack_[i];
        if (!e.iterAlloca) continue;  // indexed-array fast path
        if (e.finallyDepth < (int)finallyStack_.size()) continue;
        auto iter = builder_.createLoad(HIRType::makeObject(), e.iterAlloca);
        emitCloseOneIterator(iter, e.isAwait);
    }
}

void ASTToHIR::visitForOfStatement(ast::ForOfStatement* node) {
    setSourceLine(node);
    // For-of loop: iterate over iterable (arrays or generators)
    auto* condBlock = createBlock("forof.cond");
    auto* bodyBlock = createBlock("forof.body");
    auto* updateBlock = createBlock("forof.update");
    auto* closeBlock = createBlock("forof.close");
    auto* endBlock = createBlock("forof.end");

    // Push loop context (continue -> update, break -> CLOSE). ECMA-262 7.4.8
    // IteratorClose: an abrupt loop completion (break) must call
    // iterator.return() before leaving; closeBlock does that (a no-op branch
    // for the indexed-array path) and falls through to endBlock. Normal
    // exhaustion (done === true) jumps straight to endBlock — an exhausted
    // iterator is NOT closed.
    // Track this loop's iterator for IteratorClose on crossing abrupt
    // completions (labeled break/continue of an outer construct, return).
    // The entry is pushed BEFORE the LoopContext so forofCloseFrom (the
    // stack size after the push) EXCLUDES this loop's own iterator: an
    // unlabeled/labeled break targeting THIS loop closes it in closeBlock,
    // and a continue keeps it open.
    forofIterStack_.push_back({nullptr, node->isAwait, (int)finallyStack_.size()});
    LoopContext ctx = {updateBlock, closeBlock, withDepth_, tryDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()};
    loopStack_.push(ctx);
    breakTargetStack_.push(closeBlock);
    breakTargetMeta_.push({tryDepth_, withDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()});
    // Set by the iterator-protocol path; closeBlock calls iterator.return().
    std::shared_ptr<HIRValue> forofCloseIterAlloca = nullptr;

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

    // Check if this is a Generator/AsyncGenerator/Iterator - use iterator protocol instead of array indexing
    bool isGenerator = iterable->type && iterable->type->kind == HIRTypeKind::Class &&
        (iterable->type->className == "Generator" || iterable->type->className == "AsyncGenerator");
    // Object-typed iterables (e.g., Map.keys() returns an iterator object) also use .next()
    bool isIteratorObject = !isGenerator && iterable->type &&
        iterable->type->kind == HIRTypeKind::Object;

    // Detect iterator-returning method calls: map.keys(), map.values(), map.entries(),
    // arr.entries(), arr.keys(), arr.values() - these return iterator objects with .next()
    // even though the TypeScript type analyzer may report them as Any before MethodResolutionPass
    if (!isGenerator && !isIteratorObject) {
        if (auto* callExpr = dynamic_cast<ast::CallExpression*>(iterExpr)) {
            if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(callExpr->callee.get())) {
                const auto& methodName = propAccess->name;
                if (methodName == "keys" || methodName == "values" || methodName == "entries") {
                    // Check if the object is a Map, Set, or Array by looking up its variable type
                    if (auto* objIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
                        auto* varInfo = lookupVariableInfo(objIdent->name);
                        if (varInfo) {
                            // For alloca variables, elemType holds the actual type;
                            // for direct variables, value->type holds the type
                            auto varTypePtr = varInfo->isAlloca ? varInfo->elemType :
                                (varInfo->value ? varInfo->value->type : nullptr);
                            if (varTypePtr) {
                                auto kind = varTypePtr->kind;
                                if (kind == HIRTypeKind::Map || kind == HIRTypeKind::Set ||
                                    kind == HIRTypeKind::Array) {
                                    isIteratorObject = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Any-typed iterables: route through the iterator protocol.
    bool isAnyIterable = !isGenerator && !isIteratorObject && iterable->type &&
        iterable->type->kind == HIRTypeKind::Any;
    if (isAnyIterable) {
        isIteratorObject = true;
    }
    // Statically-typed PRIMITIVES (`for (x of true)` / `of 5`): not iterable
    // — route through ts_iterator_get, whose primitive tail throws the spec
    // TypeError. The array fast path read them as length-0 and silently
    // skipped the loop (for-of elision-val families).
    if (!isGenerator && !isIteratorObject && iterable->type &&
        (iterable->type->kind == HIRTypeKind::Bool ||
         iterable->type->kind == HIRTypeKind::Int64 ||
         iterable->type->kind == HIRTypeKind::Float64)) {
        iterable = boxValueIfNeeded(iterable);
        isIteratorObject = true;
    }
    // String-typed iterables: also route through the iterator protocol so
    // ts_iterator_get can return a proper code-point iterator. Without this,
    // we fell into the array fast path (createArrayLength + getElem) which
    // reads garbage from TsString and infinite-loops.
    bool isStringIterable = !isGenerator && !isIteratorObject && iterable->type &&
        iterable->type->kind == HIRTypeKind::String;
    if (isStringIterable) {
        isIteratorObject = true;
    }
    // Set/Map-typed iterables used directly (`for (x of set)`, `for ([k,v] of map)`)
    // must use the iterator protocol: the array fast path (ts_array_length + index)
    // reads a TsMap-backed Set/Map as an array and yields nothing (the loop body
    // never runs). ts_iterator_get now builds the proper Set-values / Map-entries
    // iterator.
    bool isSetOrMapIterable = !isGenerator && !isIteratorObject && iterable->type &&
        (iterable->type->kind == HIRTypeKind::Set ||
         iterable->type->kind == HIRTypeKind::Map);
    if (isSetOrMapIterable) {
        isIteratorObject = true;
    }

    // For every iterator-protocol path, coerce to the actual iterator via
    // ts_iterator_get. Handles three cases:
    //   1. Value is already an iterator (has .next) → return as-is.
    //   2. Value is iterable (has [Symbol.iterator]) → call it, return result.
    //   3. Neither → return iterable unchanged; subsequent .next() returns
    //      undefined, .done is truthy, loop exits. Safe for non-iterables.
    // Generators/Map.keys()/custom iterables all go through this path uniformly.
    if (isGenerator || isIteratorObject) {
        // for-await-of: GetIterator with the ASYNC hint — @@asyncIterator is
        // preferred over @@iterator (ES 14.7.5.6 ForIn/OfHeadEvaluation
        // iteratorKind async-iterate); the sync fallback's next() results and
        // values are Awaited by the loop (AsyncFromSyncIterator core).
        iterable = builder_.createCall(
            node->isAwait ? "ts_for_await_iterator_get" : "ts_iterator_get",
            {iterable}, HIRType::makeAny());
    }

    isGenerator = isGenerator || isIteratorObject;

    if (isGenerator) {
        // Generator iteration: call .next() in a loop, check .done, get .value
        // Store result in an alloca so we can access it in both cond and body blocks
        auto resultAlloca = builder_.createAlloca(HIRType::makeObject(), "forof.result");

        // Store the iterator object in an alloca so it survives across yield
        // resume points. In a generator/async function the cond block can be
        // re-entered from a yield_resume path that doesn't dominate the SSA
        // value produced by ts_iterator_get above. Allocas are hoisted to the
        // function entry, so the stored value is visible from every block.
        auto iterAlloca = builder_.createAlloca(HIRType::makeObject(), "forof.iter");
        builder_.createStore(iterable, iterAlloca);
        forofCloseIterAlloca = iterAlloca;  // closeBlock reads it (break path)
        // Crossing abrupt completions (labeled break/continue, return) and
        // the throw-close catch region read the iterator through this entry.
        forofIterStack_.back().iterAlloca = iterAlloca;

        builder_.createBranch(condBlock);

        // Condition: call gen.next(), check if result.done is true
        builder_.setInsertPoint(condBlock);
        currentBlock_ = condBlock;
        auto iterReload = builder_.createLoad(HIRType::makeObject(), iterAlloca);
        auto nextResult = builder_.createCallMethod(iterReload, "next", {}, HIRType::makeObject());
        // for-await-of: next() on an async iterator returns a PROMISE of the
        // iteration result — await it before reading .done/.value (reading
        // them off the promise yields undefined and the loop never ends).
        // ts_promise_await passes non-promise results through, so sync
        // iterators under for-await are unaffected.
        if (node->isAwait) {
            nextResult = builder_.createAwait(nextResult);
        }
        // ES 7.4.3 IteratorStep: a primitive next() result is a TypeError
        // (previously `.done` of a primitive was falsy forever -> infinite loop).
        builder_.createCall("ts_iterator_step_require_object", {nextResult},
                            HIRType::makeVoid());
        builder_.createStore(nextResult, resultAlloca);
        auto doneVal = builder_.createGetPropStatic(nextResult, "done", HIRType::makeAny());
        // condBranch handles boxed value -> bool conversion via ts_value_to_bool
        builder_.createCondBranch(doneVal, endBlock, bodyBlock);

        // Body: get value and execute body
        builder_.setInsertPoint(bodyBlock);
        currentBlock_ = bodyBlock;
        auto resultVal = builder_.createLoad(HIRType::makeObject(), resultAlloca);
        auto elemVal = builder_.createGetPropStatic(resultVal, "value", HIRType::makeAny());
        // for-await-of additionally awaits each VALUE (AsyncFromSyncIterator
        // semantics — `for await (v of [p1, p2])` binds the settled values).
        if (node->isAwait) {
            elemVal = builder_.createAwait(elemVal);
        }

        // ES 14.7.5.7 steps 6.f/6.k: an ABRUPT completion from the binding
        // initialization or the loop body must IteratorClose(iterator) with
        // the throw completion — return() is called and every error it raises
        // is SWALLOWED, the original exception rethrows (7.4.6 steps 5-6).
        // Arm a synthetic handler around binding+body (NOT around next()/
        // IteratorValue — those propagate without closing, steps 6.c/6.e).
        // tryDepth_/tryScopeStack_ participate exactly like a real try so
        // break/continue/return inside the body pop this handler through the
        // existing conventions and yields re-arm it on resume.
        auto* forofCatchBB = createBlock("forof.throwclose");
        auto* forofBodyRunBB = createBlock("forof.bodyrun");
        auto forofIsExc = builder_.createSetupTry(forofCatchBB);
        builder_.createCondBranch(forofIsExc, forofCatchBB, forofBodyRunBB);
        builder_.setInsertPoint(forofBodyRunBB);
        currentBlock_ = forofBodyRunBB;
        tryDepth_++;
        tryScopeStack_.push_back({currentFunction_, forofCatchBB});

        // Bind to loop variable (supports simple, array destructuring, object destructuring)
        if (node->initializer) {
            auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
            if (varDecl) {
                if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                    defineVariable(ident->name, elemVal);
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(varDecl->name.get())) {
                    lowerArrayBindingPattern(arrPat, elemVal);
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(varDecl->name.get())) {
                    lowerObjectBindingPattern(objPat, elemVal);
                }
            } else {
                // Assignment-target form: `for (lhs of iter)` where lhs is
                // an existing variable, member access, element access, or
                // an array/object destructuring pattern. The parser hands
                // these to us as an ExpressionStatement (initializer is
                // StmtPtr, not ExprPtr), so unwrap one layer if needed.
                ast::Node* lhsNode = node->initializer.get();
                if (auto* es = dynamic_cast<ast::ExpressionStatement*>(lhsNode)) {
                    lhsNode = es->expression.get();
                }
                if (auto* lhsExpr = dynamic_cast<ast::Expression*>(lhsNode)) {
                    auto savedLast = lastValue_;
                    // Push elemVal as the synthetic RHS by stashing it as
                    // lastValue_ during a dispatch on a fake AssignmentExpression.
                    // We can't construct an AST AssignmentExpression here, so
                    // inline the destructure dispatch instead.
                    if (auto* arrLit = dynamic_cast<ast::ArrayLiteralExpression*>(lhsExpr)) {
                        // Top-level ARRAY assignment pattern: `for ([a, b.c, ...r] of ...)`.
                        // Route through the shared destructuring-assignment engine (mirrors
                        // the objLit branch below). It performs the ES 13.15.5.1 early-error
                        // checks and, for patterns whose targets are member/element
                        // References, the interleaved IteratorClose-correct lowering
                        // (ES 8.6.2 / 7.4.6): the target reference is evaluated BEFORE the
                        // iterator is stepped and the iterator is closed (return()) on an
                        // abrupt or early-normal completion. Previously a 145-line inline
                        // copy bulk-materialized every slot up front, violating evaluation
                        // order and never closing on a throw.
                        destructureAssignmentPattern(arrLit, boxValueIfNeeded(elemVal));
                    } else if (auto* objLit = dynamic_cast<ast::ObjectLiteralExpression*>(lhsExpr)) {
                        // Top-level OBJECT assignment pattern: `for ({ x } of ...)`.
                        // Route through the assignment-pattern engine (same as
                        // nested targets) — previously fell through silently and
                        // the pattern's identifiers were never assigned.
                        destructureAssignmentPattern(objLit, boxValueIfNeeded(elemVal));
                    } else if (auto* id = dynamic_cast<ast::Identifier*>(lhsExpr)) {
                        // Plain `for (x of arr)` without let/const — assign
                        // to existing variable each iteration.
                        auto* info = lookupVariableInfo(id->name);
                        if (info && info->isAlloca) {
                            builder_.createStore(elemVal, info->value, info->elemType);
                        } else if (info) {
                            auto allocaPtr = builder_.createAlloca(elemVal->type, id->name);
                            builder_.createStore(elemVal, allocaPtr, elemVal->type);
                            info->value = allocaPtr;
                            info->elemType = elemVal->type;
                            info->isAlloca = true;
                        } else {
                            defineVariable(id->name, elemVal);
                        }
                        if (currentFunction_ && isModuleGlobalVar(id->name)) {
                            builder_.createStoreGlobal(modVarName(id->name), elemVal);
                        }
                    } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(lhsExpr)) {
                        // `for (obj.prop of iter)` — ES 14.7.5.7
                        // ForIn/OfBodyEvaluation step 6.f: PutValue on a
                        // property Reference each iteration. PRIVATE targets
                        // (`for (this.#x of iter)`) route through
                        // ts_object_set_private so PrivateSet (7.3.31) brand-
                        // checks a foreign receiver -> TypeError
                        // (privatefieldset-typeerror-6). This target form was
                        // previously dropped silently (no store at all).
                        auto tobj = lowerExpression(pa->expression.get());
                        if (!pa->name.empty() && pa->name[0] == '#') {
                            emitPrivateBrandCheck(tobj, pa->name);
                            auto pk = builder_.createConstString(resolvePrivateName(pa->name));
                            builder_.createCall("ts_object_set_private",
                                {tobj, pk, boxValueIfNeeded(elemVal)}, HIRType::makeVoid());
                        } else {
                            builder_.createSetPropStatic(tobj, pa->name, elemVal);
                        }
                    } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(lhsExpr)) {
                        // `for (arr[i] of iter)` — element Reference target.
                        auto tobj = lowerExpression(ea->expression.get());
                        auto tidx = lowerExpression(ea->argumentExpression.get());
                        builder_.createSetElem(tobj, tidx, elemVal);
                    }
                    lastValue_ = savedLast;
                }
            }
        }

        lowerStatement(node->body.get());

        // End of the synthetic throw-close region: on the normal path pop
        // the handler; abrupt exits (break/continue/return/throw) already
        // popped or unwound it through the standard conventions.
        tryScopeStack_.pop_back();
        tryDepth_--;
        if (!hasTerminator()) {
            builder_.createPopHandler();
        }

        // Branch to update (if not already terminated)
        emitBranchIfNeeded(updateBlock);

        // forof.throwclose: IteratorClose with a throw completion — close
        // errors are swallowed (7.4.6 step 6.b), the ORIGINAL exception
        // rethrows to the enclosing handler.
        builder_.setInsertPoint(forofCatchBB);
        currentBlock_ = forofCatchBB;
        {
            auto forofExc = builder_.createGetException();
            builder_.createClearException();
            auto closeIter = builder_.createLoad(HIRType::makeObject(),
                                                 forofCloseIterAlloca);
            builder_.createCall("ts_iterator_close_quiet", {closeIter},
                                HIRType::makeVoid());
            builder_.createThrow(forofExc);
        }

        // Update block: just jump back to cond (next call happens there)
        builder_.setInsertPoint(updateBlock);
        currentBlock_ = updateBlock;
        builder_.createBranch(condBlock);
    } else {
        // Array iteration: use index-based access

        // Create index variable (alloca for SSA)
        auto indexAlloca = builder_.createAlloca(HIRType::makeInt64(), "forof.idx");
        auto zero = builder_.createConstInt(0);
        builder_.createStore(zero, indexAlloca);

        builder_.createBranch(condBlock);

        // Condition: index < length
        builder_.setInsertPoint(condBlock);
        currentBlock_ = condBlock;
        // ES 23.1.5.2.1 ArrayIterator: each next() reads the array's CURRENT
        // length fresh — re-read ts_array_length inside the condition block so
        // appending during iteration visits new elements and shrinking stops
        // early. Hoisting this before the loop cached a stale length.
        auto lenVal = builder_.createArrayLength(iterable);
        auto indexVal = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
        auto cond = builder_.createCmpLtI64(indexVal, lenVal);
        builder_.createCondBranch(cond, bodyBlock, endBlock);

        // Body: get element and execute body
        builder_.setInsertPoint(bodyBlock);
        currentBlock_ = bodyBlock;

        // Get current element
        auto currentIndex = builder_.createLoad(HIRType::makeInt64(), indexAlloca);
        auto elemVal = builder_.createGetElem(iterable, currentIndex);

        // Bind to loop variable (supports simple, array destructuring, object destructuring)
        if (node->initializer) {
            auto* varDecl = dynamic_cast<ast::VariableDeclaration*>(node->initializer.get());
            if (varDecl) {
                if (auto* ident = dynamic_cast<ast::Identifier*>(varDecl->name.get())) {
                    defineVariable(ident->name, elemVal);
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(varDecl->name.get())) {
                    lowerArrayBindingPattern(arrPat, elemVal);
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(varDecl->name.get())) {
                    lowerObjectBindingPattern(objPat, elemVal);
                }
            } else {
                // Assignment-target form: `for (lhs of iter)` where lhs is
                // an existing variable, member access, element access, or
                // an array/object destructuring pattern. The parser hands
                // these to us as an ExpressionStatement (initializer is
                // StmtPtr, not ExprPtr), so unwrap one layer if needed.
                ast::Node* lhsNode = node->initializer.get();
                if (auto* es = dynamic_cast<ast::ExpressionStatement*>(lhsNode)) {
                    lhsNode = es->expression.get();
                }
                if (auto* lhsExpr = dynamic_cast<ast::Expression*>(lhsNode)) {
                    auto savedLast = lastValue_;
                    // Push elemVal as the synthetic RHS by stashing it as
                    // lastValue_ during a dispatch on a fake AssignmentExpression.
                    // We can't construct an AST AssignmentExpression here, so
                    // inline the destructure dispatch instead.
                    if (auto* arrLit = dynamic_cast<ast::ArrayLiteralExpression*>(lhsExpr)) {
                        // Top-level ARRAY assignment pattern: `for ([a, b.c, ...r] of ...)`.
                        // Route through the shared destructuring-assignment engine (mirrors
                        // the objLit branch below). It performs the ES 13.15.5.1 early-error
                        // checks and, for patterns whose targets are member/element
                        // References, the interleaved IteratorClose-correct lowering
                        // (ES 8.6.2 / 7.4.6): the target reference is evaluated BEFORE the
                        // iterator is stepped and the iterator is closed (return()) on an
                        // abrupt or early-normal completion. Previously a 145-line inline
                        // copy bulk-materialized every slot up front, violating evaluation
                        // order and never closing on a throw.
                        destructureAssignmentPattern(arrLit, boxValueIfNeeded(elemVal));
                    } else if (auto* objLit = dynamic_cast<ast::ObjectLiteralExpression*>(lhsExpr)) {
                        // Top-level OBJECT assignment pattern: `for ({ x } of ...)`.
                        // Route through the assignment-pattern engine (same as
                        // nested targets) — previously fell through silently and
                        // the pattern's identifiers were never assigned.
                        destructureAssignmentPattern(objLit, boxValueIfNeeded(elemVal));
                    } else if (auto* id = dynamic_cast<ast::Identifier*>(lhsExpr)) {
                        // Plain `for (x of arr)` without let/const — assign
                        // to existing variable each iteration.
                        auto* info = lookupVariableInfo(id->name);
                        if (info && info->isAlloca) {
                            builder_.createStore(elemVal, info->value, info->elemType);
                        } else if (info) {
                            auto allocaPtr = builder_.createAlloca(elemVal->type, id->name);
                            builder_.createStore(elemVal, allocaPtr, elemVal->type);
                            info->value = allocaPtr;
                            info->elemType = elemVal->type;
                            info->isAlloca = true;
                        } else {
                            defineVariable(id->name, elemVal);
                        }
                        if (currentFunction_ && isModuleGlobalVar(id->name)) {
                            builder_.createStoreGlobal(modVarName(id->name), elemVal);
                        }
                    } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(lhsExpr)) {
                        // `for (obj.prop of iter)` — ES 14.7.5.7
                        // ForIn/OfBodyEvaluation step 6.f: PutValue on a
                        // property Reference each iteration. PRIVATE targets
                        // (`for (this.#x of iter)`) route through
                        // ts_object_set_private so PrivateSet (7.3.31) brand-
                        // checks a foreign receiver -> TypeError
                        // (privatefieldset-typeerror-6). This target form was
                        // previously dropped silently (no store at all).
                        auto tobj = lowerExpression(pa->expression.get());
                        if (!pa->name.empty() && pa->name[0] == '#') {
                            emitPrivateBrandCheck(tobj, pa->name);
                            auto pk = builder_.createConstString(resolvePrivateName(pa->name));
                            builder_.createCall("ts_object_set_private",
                                {tobj, pk, boxValueIfNeeded(elemVal)}, HIRType::makeVoid());
                        } else {
                            builder_.createSetPropStatic(tobj, pa->name, elemVal);
                        }
                    } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(lhsExpr)) {
                        // `for (arr[i] of iter)` — element Reference target.
                        auto tobj = lowerExpression(ea->expression.get());
                        auto tidx = lowerExpression(ea->argumentExpression.get());
                        builder_.createSetElem(tobj, tidx, elemVal);
                    }
                    lastValue_ = savedLast;
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
    }

    loopStack_.pop();
    breakTargetStack_.pop();
    breakTargetMeta_.pop();
    forofIterStack_.pop_back();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    // closeBlock: break lands here. Iterator-protocol loops perform
    // IteratorClose with a break (non-throw) completion (ES 7.4.6): strict
    // semantics — GetMethod/call errors propagate, a non-object return()
    // result is a TypeError; for-await-of additionally Awaits the result
    // (ES 7.4.7). The indexed-array path has no iterator: plain fall-through.
    builder_.setInsertPoint(closeBlock);
    currentBlock_ = closeBlock;
    if (forofCloseIterAlloca) {
        auto closeIter = builder_.createLoad(HIRType::makeObject(), forofCloseIterAlloca);
        emitCloseOneIterator(closeIter, node->isAwait);
    }
    builder_.createBranch(endBlock);

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitForInStatement(ast::ForInStatement* node) {
    setSourceLine(node);
    // For-in loop: iterate over object keys
    // Implementation: Get Object.keys(obj), then iterate over the array
    auto* condBlock = createBlock("forin.cond");
    auto* bodyBlock = createBlock("forin.body");
    auto* updateBlock = createBlock("forin.update");
    auto* endBlock = createBlock("forin.end");

    // Push loop context (continue -> update, break -> end)
    LoopContext ctx = {updateBlock, endBlock, withDepth_, tryDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);
    breakTargetMeta_.push({tryDepth_, withDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()});

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

    // Get keys array: own + inherited enumerable string keys (for-in walks the
    // prototype chain, unlike Object.keys which is own-only).
    auto keys = builder_.createCall("ts_object_for_in_keys", {obj}, HIRType::makeArray(HIRType::makeString()));

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
        } else {
            // Assignment-target form: `for (lhs in obj)` where lhs is an
            // existing variable or a property/element Reference — ES 14.7.5.7
            // ForIn/OfBodyEvaluation step 6.f PutValue each iteration.
            // Mirrors the for-of assignment-target handling. PRIVATE property
            // targets (`for (this.#x in obj)`) route through
            // ts_object_set_private so PrivateSet (7.3.31) brand-checks a
            // foreign receiver -> TypeError (privatefieldset-typeerror-7).
            ast::Node* lhsNode = node->initializer.get();
            if (auto* es = dynamic_cast<ast::ExpressionStatement*>(lhsNode)) {
                lhsNode = es->expression.get();
            }
            if (auto* id = dynamic_cast<ast::Identifier*>(lhsNode)) {
                auto* info = lookupVariableInfo(id->name);
                if (info && info->isAlloca) {
                    builder_.createStore(key, info->value, info->elemType);
                } else if (info) {
                    auto allocaPtr = builder_.createAlloca(key->type, id->name);
                    builder_.createStore(key, allocaPtr, key->type);
                    info->value = allocaPtr;
                    info->elemType = key->type;
                    info->isAlloca = true;
                } else {
                    defineVariable(id->name, key);
                }
                if (currentFunction_ && isModuleGlobalVar(id->name)) {
                    builder_.createStoreGlobal(modVarName(id->name), key);
                }
            } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(lhsNode)) {
                auto tobj = lowerExpression(pa->expression.get());
                if (!pa->name.empty() && pa->name[0] == '#') {
                    emitPrivateBrandCheck(tobj, pa->name);
                    auto pk = builder_.createConstString(resolvePrivateName(pa->name));
                    builder_.createCall("ts_object_set_private",
                        {tobj, pk, boxValueIfNeeded(key)}, HIRType::makeVoid());
                } else {
                    builder_.createSetPropStatic(tobj, pa->name, key);
                }
            } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(lhsNode)) {
                auto tobj = lowerExpression(ea->expression.get());
                auto tidx = lowerExpression(ea->argumentExpression.get());
                builder_.createSetElem(tobj, tidx, key);
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
    breakTargetStack_.pop();
    breakTargetMeta_.pop();
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitBreakStatement(ast::BreakStatement* node) {
    setSourceLine(node);
    // ES 14.15: if this break exits the body of an enclosing try-with-finally,
    // run those finallys (in innermost-first order) before branching to the
    // loop/switch break target. routeAbruptThroughFinally returns true and
    // emits the routing branch when at least one finally must run; otherwise we
    // fall through to the original direct-branch fast path.
    {
        HIRBlock* target = nullptr;
        int tTry = 0, tWith = withDepth_, tFin = (int)finallyStack_.size();
        int tIterFrom = (int)forofIterStack_.size();
        if (!node->label.empty()) {
            auto it = labeledLoops_.find(node->label);
            if (it != labeledLoops_.end()) {
                target = it->second.breakTarget;
                tTry = it->second.tryDepth;
                tWith = it->second.withDepth;
                tFin = it->second.finallyDepth;
                tIterFrom = it->second.forofCloseFrom;
            }
        } else if (!breakTargetStack_.empty()) {
            target = breakTargetStack_.top();
            const auto& m = breakTargetMeta_.top();
            tTry = m.tryDepth;
            tWith = m.withDepth;
            tFin = m.finallyDepth;
            tIterFrom = m.forofCloseFrom;
        }
        // ES 14.7.5.7 step 6.k: a break crossing INNER for-of loops closes
        // their iterators, innermost first. The target loop's own iterator
        // (when it is a for-of) is excluded — its closeBlock closes it.
        if (target) emitOpenIteratorCloses(tIterFrom);
        if (target &&
            routeAbruptThroughFinally(/*isReturn=*/false, target, tTry, tWith,
                                      tFin, /*retVal=*/nullptr)) {
            return;
        }
    }
    for (int i = 0; i < tryDepth_; i++) {
        builder_.createPopHandler();
    }
    // Unwind with-scopes entered INSIDE the target construct (ES 14.11).
    auto emitWithUnwind = [&](int targetDepth) {
        int n = withDepth_ - targetDepth;
        if (n > 0) {
            builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(n)}, HIRType::makeVoid());
        }
    };
    if (!node->label.empty()) {
        auto it = labeledLoops_.find(node->label);
        if (it != labeledLoops_.end()) {
            emitWithUnwind(it->second.withDepth);
            builder_.createBranch(it->second.breakTarget);
        }
    } else if (!breakTargetStack_.empty()) {
        emitWithUnwind(loopStack_.empty() ? withDepth_ : loopStack_.top().withDepth);
        builder_.createBranch(breakTargetStack_.top());
    }
}

void ASTToHIR::visitContinueStatement(ast::ContinueStatement* node) {
    setSourceLine(node);
    // ES 14.15: a `continue` that leaves an enclosing try-with-finally body must
    // run those finallys before jumping to the loop's continue target.
    {
        HIRBlock* target = nullptr;
        int tTry = 0, tWith = withDepth_, tFin = (int)finallyStack_.size();
        int tIterFrom = (int)forofIterStack_.size();
        if (!node->label.empty()) {
            auto it = labeledLoops_.find(node->label);
            if (it != labeledLoops_.end()) {
                target = it->second.continueTarget;
                tTry = it->second.tryDepth;
                tWith = it->second.withDepth;
                tFin = it->second.finallyDepth;
                tIterFrom = it->second.forofCloseFrom;
            }
        } else if (!loopStack_.empty()) {
            target = loopStack_.top().continueTarget;
            tTry = loopStack_.top().tryDepth;
            tWith = loopStack_.top().withDepth;
            tFin = loopStack_.top().finallyDepth;
            tIterFrom = loopStack_.top().forofCloseFrom;
        }
        // ES 14.7.5.7 step 6.k: a labeled continue that exits INNER for-of
        // loops closes their iterators (iterator-close-via-continue). The
        // continued loop's own iterator stays open.
        if (target) emitOpenIteratorCloses(tIterFrom);
        if (target &&
            routeAbruptThroughFinally(/*isReturn=*/false, target, tTry, tWith,
                                      tFin, /*retVal=*/nullptr)) {
            return;
        }
    }
    for (int i = 0; i < tryDepth_; i++) {
        builder_.createPopHandler();
    }
    auto emitWithUnwind = [&](int targetDepth) {
        int n = withDepth_ - targetDepth;
        if (n > 0) {
            builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(n)}, HIRType::makeVoid());
        }
    };
    if (!node->label.empty()) {
        auto it = labeledLoops_.find(node->label);
        if (it != labeledLoops_.end()) {
            emitWithUnwind(it->second.withDepth);
            builder_.createBranch(it->second.continueTarget);
        }
    } else if (!loopStack_.empty()) {
        emitWithUnwind(loopStack_.top().withDepth);
        builder_.createBranch(loopStack_.top().continueTarget);
    }
}

void ASTToHIR::visitLabeledStatement(ast::LabeledStatement* node) {
    setSourceLine(node);
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
    setSourceLine(node);
    auto switchVal = lowerExpression(node->expression.get());

    // The case block is ONE lexical scope shared by all clauses (ES 14.12).
    // Push it BEFORE the dispatch is emitted (case selector expressions
    // evaluate inside it) and pre-seed clause-level let/const with the TDZ
    // sentinel here, where the stores still execute before any case body —
    // reading a clause let whose declaration hasn't run throws
    // ReferenceError (switch-tdz family).
    pushScope();
    for (auto& clause : node->clauses) {
        std::vector<ast::StmtPtr>* stmts = nullptr;
        if (auto* cc = dynamic_cast<ast::CaseClause*>(clause.get())) stmts = &cc->statements;
        else if (auto* dc = dynamic_cast<ast::DefaultClause*>(clause.get())) stmts = &dc->statements;
        if (!stmts) continue;
        for (auto& stmt : *stmts) {
            auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get());
            if (!vd || (vd->varKind != ast::VarKind::Let &&
                        vd->varKind != ast::VarKind::Const)) continue;
            auto* ident = dynamic_cast<ast::Identifier*>(vd->name.get());
            if (!ident) continue;
            if (scopes_.back().variables.count(ident->name)) continue;
            auto allocaVal = builder_.createAlloca(HIRType::makeAny(), ident->name);
            auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
            builder_.createStore(tdz, allocaVal, HIRType::makeAny());
            defineVariableAlloca(ident->name, allocaVal, HIRType::makeAny());
            auto it = scopes_.back().variables.find(ident->name);
            if (it != scopes_.back().variables.end()) it->second.isTDZ = true;
        }
    }

    auto* endBlock = createBlock("switch.end");
    switchStack_.push({endBlock, {}, nullptr});
    breakTargetStack_.push(endBlock);
    breakTargetMeta_.push({tryDepth_, withDepth_, (int)finallyStack_.size(), (int)forofIterStack_.size()});

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

    // Classify the case expressions to pick a lowering strategy:
    //   - all numeric literals -> dense integer switch (fast path)
    //   - all string literals  -> ts_string_eq if-else chain (cheap value eq)
    //   - anything else (identifiers, member access, mixed types) -> general
    //     strict-equality (===) if-else chain. JS evaluates case expressions
    //     top-to-bottom and stops at the first strict-equal match; the chain
    //     models exactly that. The previous code only emitted comparisons for
    //     StringLiteral/NumericLiteral case labels and SILENTLY DROPPED any
    //     non-literal case (e.g. `case dateTag:` where dateTag is a variable),
    //     so such cases never matched and fell through -- breaking lodash's
    //     equalByTag (tag constants) and any switch over computed values.
    bool anyCaseExpr = false, allNumeric = true, allString = true;
    for (auto& clause : node->clauses) {
        auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
        if (caseClause && caseClause->expression) {
            anyCaseExpr = true;
            if (!dynamic_cast<ast::NumericLiteral*>(caseClause->expression.get())) allNumeric = false;
            if (!dynamic_cast<ast::StringLiteral*>(caseClause->expression.get())) allString = false;
        }
    }

    if (anyCaseExpr && allNumeric) {
        // Dense integer switch.
        std::vector<std::pair<int64_t, HIRBlock*>> cases;
        size_t blockIdx = 0;
        for (auto& clause : node->clauses) {
            auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());
            if (caseClause && caseClause->expression) {
                auto* numLit = dynamic_cast<ast::NumericLiteral*>(caseClause->expression.get());
                if (numLit && blockIdx < caseBlocks.size()) {
                    cases.push_back({static_cast<int64_t>(numLit->value), caseBlocks[blockIdx]});
                }
            }
            blockIdx++;
        }
        builder_.createSwitch(switchVal, defaultBlock, cases);
    } else {
        // If-else comparison chain. switchVal may be boxed (any/TsValue*).
        // For all-string-literal switches keep ts_string_eq (HIRToLLVM's
        // handler unboxes via ts_value_get_string). Otherwise evaluate each
        // case expression at runtime and compare with full === semantics.
        bool useStringEq = anyCaseExpr && allString;
        std::shared_ptr<HIRValue> boxedSwitch;
        if (!useStringEq) boxedSwitch = boxValueIfNeeded(switchVal);

        size_t blockIdx = 0;
        for (auto& clause : node->clauses) {
            auto* caseClause = dynamic_cast<ast::CaseClause*>(clause.get());

            if (caseClause && caseClause->expression && blockIdx < caseBlocks.size()) {
                std::shared_ptr<HIRValue> cmpResult;
                if (useStringEq) {
                    auto* strLit = dynamic_cast<ast::StringLiteral*>(caseClause->expression.get());
                    auto caseStr = builder_.createConstString(strLit->value);
                    cmpResult = builder_.createCall("ts_string_eq",
                        {switchVal, caseStr}, HIRType::makeBool());
                } else {
                    // Evaluate the case expression in the current check block
                    // so side effects occur in order and only until a match.
                    auto caseVal = lowerExpression(caseClause->expression.get());
                    auto boxedCase = boxValueIfNeeded(caseVal);
                    cmpResult = builder_.createCall("ts_value_strict_eq",
                        {boxedSwitch, boxedCase}, HIRType::makeAny());
                }

                // Determine the "next check" block
                HIRBlock* nextCheckBlock = nullptr;
                for (size_t j = blockIdx + 1; j < node->clauses.size(); ++j) {
                    auto* nextCase = dynamic_cast<ast::CaseClause*>(node->clauses[j].get());
                    if (nextCase && nextCase->expression) {
                        nextCheckBlock = createBlock("switch.check");
                        break;
                    }
                }
                if (!nextCheckBlock) nextCheckBlock = defaultBlock;

                builder_.createCondBranch(cmpResult, caseBlocks[blockIdx], nextCheckBlock);

                // Continue emitting checks from the next check block
                builder_.setInsertPoint(nextCheckBlock);
                currentBlock_ = nextCheckBlock;
            }
            blockIdx++;
        }

        // If we're in a check block (not the default block itself) without a
        // terminator, branch to default.
        if (!hasTerminator() && currentBlock_ != defaultBlock) {
            builder_.createBranch(defaultBlock);
        }
    }

    // Generate code for each case. ES 14.12: ALL clauses share ONE lexical
    // block scope — without it, a block-level `function f(){}` in a clause
    // leaked its binding into the function scope (annexB skip-early-err-switch:
    // reading `f` after the switch must throw ReferenceError). The scope is
    // now pushed at visitor entry (before the dispatch) so TDZ seeds and
    // case-selector expressions live inside it.
    size_t blockIdx = 0;
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
    popScope();

    switchStack_.pop();
    breakTargetStack_.pop();
    breakTargetMeta_.pop();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

bool ASTToHIR::routeAbruptThroughFinally(bool isReturn, HIRBlock* target,
        int targetTryDepth, int targetWithDepth, int crossFinallyDepth,
        std::shared_ptr<HIRValue> retVal) {
    // Nothing to do unless at least one enclosing finally lies between the
    // abrupt statement and its destination (finallyStack_ indices at or above
    // crossFinallyDepth). The caller then takes its original direct-branch path.
    if ((int)finallyStack_.size() <= crossFinallyDepth) {
        return false;
    }
    // Assign / look up the completion discriminant for this destination.
    // 1 == return; each distinct break/continue target gets its own id (>=2).
    int discVal;
    if (isReturn) {
        discVal = 1;
    } else {
        auto it = completionDestId_.find(target);
        if (it == completionDestId_.end()) {
            CompletionDest cd{nextCompletionId_++, targetTryDepth, targetWithDepth,
                              crossFinallyDepth};
            completionDestId_[target] = cd;
            discVal = cd.id;
        } else {
            discVal = it->second.id;
        }
    }
    // Record the completion on every finally it will pass through so each
    // finally's epilogue emits a dispatch case for it.
    for (int i = crossFinallyDepth; i < (int)finallyStack_.size(); i++) {
        if (isReturn) finallyStack_[i].sawReturn = true;
        else finallyStack_[i].targets.insert(target);
    }
    // Leave the try bodies between here and the innermost crossed finally: pop
    // their handlers plus any with-scopes opened inside them, store the pending
    // completion, and branch into that finally. Its epilogue forwards the
    // completion outward (see emitFinallyDispatch).
    FinallyContext& inner = finallyStack_.back();
    for (int i = 0; i < tryDepth_ - inner.handlerOutside; i++) {
        builder_.createPopHandler();
    }
    int wn = withDepth_ - inner.withOutside;
    if (wn > 0) {
        builder_.createCall("ts_with_pop_n",
            {builder_.createConstInt(wn)}, HIRType::makeVoid());
    }
    builder_.createStore(builder_.createConstInt(discVal), inner.discAlloca);
    if (isReturn && retVal) {
        builder_.createStore(retVal, inner.valAlloca);
    }
    builder_.createBranch(inner.finallyBB);
    return true;
}

void ASTToHIR::emitFinallyDispatch(const FinallyContext& F, HIRBlock* mergeBB,
                                   std::shared_ptr<HIRValue> pendingExc) {
    auto disc = builder_.createLoad(HIRType::makeInt64(), F.discAlloca);

    HIRBlock* normalBB = createBlock("finally.normal");
    std::vector<std::pair<int64_t, HIRBlock*>> cases;

    HIRBlock* retBB = nullptr;
    if (F.sawReturn) {
        retBB = createBlock("finally.ret");
        cases.push_back({1, retBB});
    }
    std::vector<std::pair<HIRBlock*, HIRBlock*>> targetBlocks;  // (dest, dispatchBB)
    for (HIRBlock* t : F.targets) {
        auto cdIt = completionDestId_.find(t);
        if (cdIt == completionDestId_.end()) continue;  // defensive
        HIRBlock* tb = createBlock("finally.brk");
        cases.push_back({cdIt->second.id, tb});
        targetBlocks.push_back({t, tb});
    }
    builder_.createSwitch(disc, normalBB, cases);

    // disc == 0: normal completion, or a pending exception to rethrow (the
    // original try-finally-without-abrupt behaviour, preserved exactly).
    builder_.setInsertPoint(normalBB);
    currentBlock_ = normalBB;
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

    // Helper: pop handlers/with-scopes from this finally's outside level down to
    // `toHandler`/`toWith` and store the completion into the enclosing finally.
    auto reRaiseTo = [&](FinallyContext& G, int discVal,
                         bool copyReturnValue) {
        for (int i = 0; i < F.handlerOutside - G.handlerOutside; i++)
            builder_.createPopHandler();
        int wn = F.withOutside - G.withOutside;
        if (wn > 0) builder_.createCall("ts_with_pop_n",
            {builder_.createConstInt(wn)}, HIRType::makeVoid());
        builder_.createStore(builder_.createConstInt(discVal), G.discAlloca);
        if (copyReturnValue) {
            auto v = builder_.createLoad(HIRType::makeAny(), F.valAlloca);
            builder_.createStore(v, G.valAlloca);
        }
        builder_.createBranch(G.finallyBB);
    };

    // disc == 1: a pending return. Re-raise to the next enclosing finally (a
    // return crosses ALL of them) or, when none remains, perform the return.
    if (retBB) {
        builder_.setInsertPoint(retBB);
        currentBlock_ = retBB;
        if (!finallyStack_.empty()) {
            FinallyContext& G = finallyStack_.back();
            G.sawReturn = true;
            reRaiseTo(G, 1, /*copyReturnValue=*/true);
        } else {
            for (int i = 0; i < F.handlerOutside; i++)
                builder_.createPopHandler();
            if (F.withOutside > 0) builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(F.withOutside)}, HIRType::makeVoid());
            if (F.withEnvEnteredOutside)
                builder_.createCall("ts_with_exit_fn", {}, HIRType::makeVoid());
            auto v = builder_.createLoad(HIRType::makeAny(), F.valAlloca);
            builder_.createReturn(v);
        }
    }

    // disc >= 2: a pending break/continue to a specific target. Re-raise to the
    // next enclosing finally only if that finally is ALSO inside the target
    // construct (its stack index >= the target's finallyDepth); otherwise the
    // target is reached now.
    for (auto& [dest, tb] : targetBlocks) {
        builder_.setInsertPoint(tb);
        currentBlock_ = tb;
        const CompletionDest& cd = completionDestId_[dest];
        bool reRaise = false;
        if (!finallyStack_.empty() &&
            (int)finallyStack_.size() - 1 >= cd.finallyDepth) {
            reRaise = true;
        }
        if (reRaise) {
            FinallyContext& G = finallyStack_.back();
            G.targets.insert(dest);
            reRaiseTo(G, cd.id, /*copyReturnValue=*/false);
        } else {
            for (int i = 0; i < F.handlerOutside - cd.tryDepth; i++)
                builder_.createPopHandler();
            int wn = F.withOutside - cd.withDepth;
            if (wn > 0) builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(wn)}, HIRType::makeVoid());
            builder_.createBranch(dest);
        }
    }
}

void ASTToHIR::visitTryStatement(ast::TryStatement* node) {
    setSourceLine(node);
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

    // ES 14.15: register this finally so break/continue/return inside the
    // try/catch body route through it (see routeAbruptThroughFinally). The
    // discriminant alloca carries the abrupt-completion kind (0 = normal/
    // exception, 1 = return, >=2 = a break/continue target); the value alloca
    // carries the pending return value. Both are initialised on the normal
    // entry path so the fall-through / exception paths read 0.
    if (finallyBB) {
        FinallyContext fctx;
        fctx.finallyBB = finallyBB;
        fctx.mergeBB = mergeBB;
        fctx.discAlloca = builder_.createAlloca(HIRType::makeInt64());
        builder_.createStore(builder_.createConstInt(0), fctx.discAlloca);
        fctx.valAlloca = builder_.createAlloca(HIRType::makeAny());
        builder_.createStore(builder_.createConstUndefined(), fctx.valAlloca);
        fctx.handlerOutside = tryDepth_;
        fctx.withOutside = withDepth_;
        fctx.withEnvEnteredOutside = withEnvEntered_;
        finallyStack_.push_back(std::move(fctx));
    }

    // Setup try: push handler and call setjmp
    // Returns true if we're coming from an exception, false on normal entry
    auto isException = builder_.createSetupTry(exceptionDest);
    builder_.createCondBranch(isException, exceptionDest, tryBB);

    // --- Try Block ---
    builder_.setInsertPoint(tryBB);
    currentBlock_ = tryBB;

    tryDepth_++;
    tryScopeStack_.push_back({currentFunction_, exceptionDest});
    // ES 14.2: the try block is its own lexical scope (like any Block).
    // Without it, a `const x` declared in one try body registered in the
    // ENCLOSING scope and a sibling try's `let x = ...; x = ...` resolved to
    // the stale const info -> spurious "Assignment to constant variable".
    pushScope();
    for (auto& stmt : node->tryBlock) {
        if (hasTerminator()) break;  // Stop if block already terminated (e.g., by throw)
        lowerStatement(stmt.get());
    }
    popScope();
    tryScopeStack_.pop_back();
    tryDepth_--;

    // Pop exception handler and branch to finally/merge
    bool tryReachedMerge = false;
    if (currentBlock_->getTerminator() == nullptr) {
        builder_.createPopHandler();
        builder_.createBranch(afterTryDest);
        tryReachedMerge = true;
    }

    // --- Catch Block ---
    bool catchReachedMerge = false;
    if (catchBB && node->catchClause) {
        builder_.setInsertPoint(catchBB);
        currentBlock_ = catchBB;

        // Get and clear the exception
        auto exception = builder_.createGetException();
        builder_.createClearException();

        // ES 14.15.2: the catch parameter and catch-body declarations live in
        // their own lexical scope — without it, a block-level `function f(){}`
        // in the catch body leaked into the function scope (annexB
        // skip-early-err-try: reading `f` after the try must throw).
        pushScope();

        // Bind exception to catch variable if present
        if (node->catchClause->variable) {
            // The variable could be an Identifier or a binding pattern
            if (auto* id = dynamic_cast<ast::Identifier*>(node->catchClause->variable.get())) {
                defineVariable(id->name, exception);
                // B.3.5: a simple catch param is transparent to the Annex B
                // var-copy of a block-level function in the catch body.
                if (auto* vi = lookupVariableInfo(id->name))
                    vi->isSimpleCatchParam = true;
            } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(node->catchClause->variable.get())) {
                lowerObjectBindingPattern(objPat, exception);
            } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(node->catchClause->variable.get())) {
                lowerArrayBindingPattern(arrPat, exception);
            }
        }

        // Execute catch block statements
        for (auto& stmt : node->catchClause->block) {
            if (hasTerminator()) break;  // Stop if block already terminated
            lowerStatement(stmt.get());
        }
        popScope();

        // Branch to finally/merge
        if (currentBlock_->getTerminator() == nullptr) {
            builder_.createBranch(afterCatchDest);
            catchReachedMerge = true;
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
        // Pop this finally off the active stack BEFORE lowering its body: an
        // abrupt statement in the finally body itself must route through the
        // ENCLOSING finallys, never through this one (and its own abrupt
        // completion overrides any pending one). Keep a copy for the epilogue.
        FinallyContext fctx = std::move(finallyStack_.back());
        finallyStack_.pop_back();

        builder_.setInsertPoint(finallyBB);
        currentBlock_ = finallyBB;

        // Execute finally block statements (own lexical scope, like try/catch)
        pushScope();
        for (auto& stmt : node->finallyBlock) {
            if (hasTerminator()) break;  // Stop if block already terminated
            lowerStatement(stmt.get());
        }
        popScope();

        // Emit the completion-dispatch epilogue. If the finally body itself
        // completed abruptly (terminator present) the pending completion is
        // discarded (ES 14.15.3 step 6). Otherwise, if any break/continue/return
        // was routed through this finally, dispatch on the discriminant;
        // if none was, fall back to the plain pending-exception rethrow/merge.
        if (currentBlock_->getTerminator() == nullptr) {
            if (fctx.sawReturn || !fctx.targets.empty()) {
                emitFinallyDispatch(fctx, mergeBB, pendingExc);
            } else if (pendingExc) {
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

    // If both try and catch terminated early (return/throw/break), no branches
    // reach the merge block. Emit a dummy return so LLVM has a valid terminator
    // (using unreachable here can cause SimplifyCFG to propagate traps into
    // reachable code paths in some edge cases).
    bool finallyReachedMerge = (finallyBB != nullptr);
    if (!tryReachedMerge && !catchReachedMerge && !finallyReachedMerge) {
        builder_.createReturn(builder_.createConstUndefined());
    }
}

void ASTToHIR::visitThrowStatement(ast::ThrowStatement* node) {
    setSourceLine(node);
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

}  // namespace ts::hir
