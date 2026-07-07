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
    if (node->expression) {
        auto retVal = lowerExpression(node->expression.get());
        // "use fast" struct value semantics: returning a struct read from an
        // lvalue yields an independent copy to the caller.
        retVal = maybeCloneStruct(retVal, node->expression.get());
        for (int i = 0; i < tryDepth_; i++) {
            builder_.createPopHandler();
        }
        if (withDepth_ > 0) {
            builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(withDepth_)}, HIRType::makeVoid());
        }
        builder_.createReturn(retVal);
    } else {
        for (int i = 0; i < tryDepth_; i++) {
            builder_.createPopHandler();
        }
        if (withDepth_ > 0) {
            builder_.createCall("ts_with_pop_n",
                {builder_.createConstInt(withDepth_)}, HIRType::makeVoid());
        }
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
    setSourceLine(node);
    auto* condBlock = createBlock("while.cond");
    auto* bodyBlock = createBlock("while.body");
    auto* endBlock = createBlock("while.end");

    // Push loop context for break/continue
    LoopContext ctx = {condBlock, endBlock, withDepth_};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

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
    LoopContext ctx = {updateBlock, endBlock, withDepth_};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

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
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    // Continue in end block
    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
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
    LoopContext ctx = {updateBlock, closeBlock, withDepth_};
    loopStack_.push(ctx);
    breakTargetStack_.push(closeBlock);
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
        iterable = builder_.createCall("ts_iterator_get", {iterable}, HIRType::makeAny());
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
                        // ECMA-262 13.15.5.1 early errors: rest element must
                        // be last and cannot have a default initializer.
                        for (size_t ri = 0; ri < arrLit->elements.size(); ++ri) {
                            auto* slot = arrLit->elements[ri].get();
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                if (ri + 1 != arrLit->elements.size()) {
                                    throw std::runtime_error("SyntaxError: Rest element must be last element in destructuring pattern");
                                }
                                if (dynamic_cast<ast::AssignmentExpression*>(sp->expression.get())) {
                                    throw std::runtime_error("SyntaxError: Rest element cannot have a default initializer");
                                }
                            }
                        }
                        // Inline minimal version of the dstr-assign code:
                        // for each element, extract source[i] (with default
                        // handling) and assign to the target.
                        builder_.createCall("ts_destructure_require_object",
                                            {elemVal}, HIRType::makeVoid());
                        // Materialize through the iterator protocol so a custom
                        // iterable is consumed via next() and closed via return()
                        // (IteratorClose). The raw index access below only works
                        // for real arrays; real arrays go through their array
                        // iterator here too -> same values. Mirrors the binding
                        // path (lowerArrayBindingPattern). Fixes the for-of/dstr
                        // array-elem-iter-*-close cluster (the element was being
                        // read as a raw array, so a custom iterable was never
                        // iterated or closed). An empty `[]` assignment pattern
                        // still GetIterator+closes (consumeCount 0 -> no next(),
                        // return() fires), unlike the binding `[]` which doesn't.
                        {
                            bool patHasRest = false; int64_t patCount = 0;
                            for (auto& el : arrLit->elements) {
                                if (dynamic_cast<ast::SpreadElement*>(el.get())) { patHasRest = true; break; }
                                patCount++;
                            }
                            elemVal = builder_.createCall("ts_destructure_iterate",
                                { elemVal, builder_.createConstInt(patCount),
                                  builder_.createConstInt(patHasRest ? 1 : 0) },
                                HIRType::makeArray(HIRType::makeAny()));
                        }
                        int64_t index = 0;
                        for (auto& slotPtr : arrLit->elements) {
                            ast::Expression* slot = slotPtr.get();
                            if (!slot || dynamic_cast<ast::OmittedExpression*>(slot)) {
                                ++index;
                                continue;
                            }
                            ast::Expression* tgt = slot;
                            std::shared_ptr<HIRValue> value;
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createCallMethod(elemVal, "slice",
                                    {idxConst}, HIRType::makeAny());
                                tgt = dynamic_cast<ast::Expression*>(sp->expression.get());
                            } else {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createGetElem(elemVal, idxConst, HIRType::makeAny());
                                if (auto* assn = dynamic_cast<ast::AssignmentExpression*>(slot)) {
                                    if (auto* defExpr = dynamic_cast<ast::Expression*>(assn->right.get())) {
                                        // Lazily evaluate the initializer ONLY when the
                                        // iterated element is undefined (ECMA-262
                                        // DestructuringAssignmentEvaluation) — the old
                                        // createSelect ran its side effects eagerly
                                        // (dstr-array-elem-init-* observe this).
                                        auto isUndef = builder_.createIsUndefined(value);
                                        value = boxValueIfNeeded(value);
                                        auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "forof_dstr_dflt");
                                        builder_.createStore(value, mergeSlot);
                                        auto* defaultBB = currentFunction_->createBlock("forof_dstr_default");
                                        auto* mergeBB = currentFunction_->createBlock("forof_dstr_merge");
                                        builder_.createCondBranch(isUndef, defaultBB, mergeBB);
                                        builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
                                        // NamedEvaluation: anonymous fn/class initializer
                                        // takes the assignment-target name.
                                        std::string savedPCDN = pendingClosureDisplayName_;
                                        if (auto* tid = dynamic_cast<ast::Identifier*>(assn->left.get())) {
                                            auto* ie = defExpr;
                                            bool anon = dynamic_cast<ast::ArrowFunction*>(ie) ||
                                                        dynamic_cast<ast::ClassExpression*>(ie);
                                            if (!anon) {
                                                if (auto* fe = dynamic_cast<ast::FunctionExpression*>(ie))
                                                    anon = fe->name.empty();
                                            }
                                            if (anon) pendingClosureDisplayName_ = tid->name;
                                        }
                                        auto defVal = lowerExpression(defExpr);
                                        pendingClosureDisplayName_ = savedPCDN;
                                        defVal = boxValueIfNeeded(defVal);
                                        builder_.createStore(defVal, mergeSlot);
                                        builder_.createBranch(mergeBB);
                                        builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
                                        value = builder_.createLoad(HIRType::makeAny(), mergeSlot);
                                    }
                                    tgt = dynamic_cast<ast::Expression*>(assn->left.get());
                                }
                            }
                            // Simple identifier assignment (most common
                            // case in for-of dstr tests). Member/element
                            // forms are rare but supported via a fallback
                            // through the assign path.
                            if (auto* id = dynamic_cast<ast::Identifier*>(tgt)) {
                                auto* info = lookupVariableInfo(id->name);
                                if (info && info->isConst) {
                                    // ES 13.15.2: destructuring-ASSIGNMENT to a
                                    // const binding throws TypeError (the raw
                                    // store below corrupted the const cell).
                                    builder_.createCall("ts_throw_const_assign",
                                                        {}, HIRType::makeAny());
                                } else if (info && info->isAlloca) {
                                    builder_.createStore(value, info->value, info->elemType);
                                } else if (info) {
                                    auto allocaPtr = builder_.createAlloca(value->type, id->name);
                                    builder_.createStore(value, allocaPtr, value->type);
                                    info->value = allocaPtr;
                                    info->elemType = value->type;
                                    info->isAlloca = true;
                                } else {
                                    defineVariable(id->name, value);
                                }
                                if (currentFunction_ && isModuleGlobalVar(id->name)) {
                                    builder_.createStoreGlobal(modVarName(id->name), value);
                                }
                            } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(pa->expression.get());
                                builder_.createSetPropStatic(obj, privateStorageKey(pa->name), value);
                            } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(ea->expression.get());
                                auto idx = lowerExpression(ea->argumentExpression.get());
                                builder_.createSetElem(obj, idx, value);
                            } else if (dynamic_cast<ast::ArrayLiteralExpression*>(tgt) ||
                                       dynamic_cast<ast::ObjectLiteralExpression*>(tgt)) {
                                // Nested destructuring target, e.g. `for ([{x}] of ...)`.
                                // Recurse through the assignment-pattern engine — was
                                // silently dropped (no require-object TypeError, nested
                                // vars left unbound).
                                destructureAssignmentPattern(tgt, boxValueIfNeeded(value));
                            }
                            ++index;
                        }
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
                    }
                    lastValue_ = savedLast;
                }
            }
        }

        lowerStatement(node->body.get());

        // Branch to update (if not already terminated)
        emitBranchIfNeeded(updateBlock);

        // Update block: just jump back to cond (next call happens there)
        builder_.setInsertPoint(updateBlock);
        currentBlock_ = updateBlock;
        builder_.createBranch(condBlock);
    } else {
        // Array iteration: use index-based access
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
                        // ECMA-262 13.15.5.1 early errors: rest element must
                        // be last and cannot have a default initializer.
                        for (size_t ri = 0; ri < arrLit->elements.size(); ++ri) {
                            auto* slot = arrLit->elements[ri].get();
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                if (ri + 1 != arrLit->elements.size()) {
                                    throw std::runtime_error("SyntaxError: Rest element must be last element in destructuring pattern");
                                }
                                if (dynamic_cast<ast::AssignmentExpression*>(sp->expression.get())) {
                                    throw std::runtime_error("SyntaxError: Rest element cannot have a default initializer");
                                }
                            }
                        }
                        // Inline minimal version of the dstr-assign code:
                        // for each element, extract source[i] (with default
                        // handling) and assign to the target.
                        builder_.createCall("ts_destructure_require_object",
                                            {elemVal}, HIRType::makeVoid());
                        // Materialize through the iterator protocol so a custom
                        // iterable is consumed via next() and closed via return()
                        // (IteratorClose). The raw index access below only works
                        // for real arrays; real arrays go through their array
                        // iterator here too -> same values. Mirrors the binding
                        // path (lowerArrayBindingPattern). Fixes the for-of/dstr
                        // array-elem-iter-*-close cluster (the element was being
                        // read as a raw array, so a custom iterable was never
                        // iterated or closed). An empty `[]` assignment pattern
                        // still GetIterator+closes (consumeCount 0 -> no next(),
                        // return() fires), unlike the binding `[]` which doesn't.
                        {
                            bool patHasRest = false; int64_t patCount = 0;
                            for (auto& el : arrLit->elements) {
                                if (dynamic_cast<ast::SpreadElement*>(el.get())) { patHasRest = true; break; }
                                patCount++;
                            }
                            elemVal = builder_.createCall("ts_destructure_iterate",
                                { elemVal, builder_.createConstInt(patCount),
                                  builder_.createConstInt(patHasRest ? 1 : 0) },
                                HIRType::makeArray(HIRType::makeAny()));
                        }
                        int64_t index = 0;
                        for (auto& slotPtr : arrLit->elements) {
                            ast::Expression* slot = slotPtr.get();
                            if (!slot || dynamic_cast<ast::OmittedExpression*>(slot)) {
                                ++index;
                                continue;
                            }
                            ast::Expression* tgt = slot;
                            std::shared_ptr<HIRValue> value;
                            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createCallMethod(elemVal, "slice",
                                    {idxConst}, HIRType::makeAny());
                                tgt = dynamic_cast<ast::Expression*>(sp->expression.get());
                            } else {
                                auto idxConst = builder_.createConstInt(index);
                                value = builder_.createGetElem(elemVal, idxConst, HIRType::makeAny());
                                if (auto* assn = dynamic_cast<ast::AssignmentExpression*>(slot)) {
                                    if (auto* defExpr = dynamic_cast<ast::Expression*>(assn->right.get())) {
                                        // Lazily evaluate the initializer ONLY when the
                                        // iterated element is undefined (ECMA-262
                                        // DestructuringAssignmentEvaluation) — the old
                                        // createSelect ran its side effects eagerly
                                        // (dstr-array-elem-init-* observe this).
                                        auto isUndef = builder_.createIsUndefined(value);
                                        value = boxValueIfNeeded(value);
                                        auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "forof_dstr_dflt");
                                        builder_.createStore(value, mergeSlot);
                                        auto* defaultBB = currentFunction_->createBlock("forof_dstr_default");
                                        auto* mergeBB = currentFunction_->createBlock("forof_dstr_merge");
                                        builder_.createCondBranch(isUndef, defaultBB, mergeBB);
                                        builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
                                        // NamedEvaluation: anonymous fn/class initializer
                                        // takes the assignment-target name.
                                        std::string savedPCDN = pendingClosureDisplayName_;
                                        if (auto* tid = dynamic_cast<ast::Identifier*>(assn->left.get())) {
                                            auto* ie = defExpr;
                                            bool anon = dynamic_cast<ast::ArrowFunction*>(ie) ||
                                                        dynamic_cast<ast::ClassExpression*>(ie);
                                            if (!anon) {
                                                if (auto* fe = dynamic_cast<ast::FunctionExpression*>(ie))
                                                    anon = fe->name.empty();
                                            }
                                            if (anon) pendingClosureDisplayName_ = tid->name;
                                        }
                                        auto defVal = lowerExpression(defExpr);
                                        pendingClosureDisplayName_ = savedPCDN;
                                        defVal = boxValueIfNeeded(defVal);
                                        builder_.createStore(defVal, mergeSlot);
                                        builder_.createBranch(mergeBB);
                                        builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
                                        value = builder_.createLoad(HIRType::makeAny(), mergeSlot);
                                    }
                                    tgt = dynamic_cast<ast::Expression*>(assn->left.get());
                                }
                            }
                            // Simple identifier assignment (most common
                            // case in for-of dstr tests). Member/element
                            // forms are rare but supported via a fallback
                            // through the assign path.
                            if (auto* id = dynamic_cast<ast::Identifier*>(tgt)) {
                                auto* info = lookupVariableInfo(id->name);
                                if (info && info->isConst) {
                                    // ES 13.15.2: destructuring-ASSIGNMENT to a
                                    // const binding throws TypeError (the raw
                                    // store below corrupted the const cell).
                                    builder_.createCall("ts_throw_const_assign",
                                                        {}, HIRType::makeAny());
                                } else if (info && info->isAlloca) {
                                    builder_.createStore(value, info->value, info->elemType);
                                } else if (info) {
                                    auto allocaPtr = builder_.createAlloca(value->type, id->name);
                                    builder_.createStore(value, allocaPtr, value->type);
                                    info->value = allocaPtr;
                                    info->elemType = value->type;
                                    info->isAlloca = true;
                                } else {
                                    defineVariable(id->name, value);
                                }
                                if (currentFunction_ && isModuleGlobalVar(id->name)) {
                                    builder_.createStoreGlobal(modVarName(id->name), value);
                                }
                            } else if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(pa->expression.get());
                                builder_.createSetPropStatic(obj, privateStorageKey(pa->name), value);
                            } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(tgt)) {
                                auto obj = lowerExpression(ea->expression.get());
                                auto idx = lowerExpression(ea->argumentExpression.get());
                                builder_.createSetElem(obj, idx, value);
                            } else if (dynamic_cast<ast::ArrayLiteralExpression*>(tgt) ||
                                       dynamic_cast<ast::ObjectLiteralExpression*>(tgt)) {
                                // Nested destructuring target, e.g. `for ([{x}] of ...)`.
                                // Recurse through the assignment-pattern engine — was
                                // silently dropped (no require-object TypeError, nested
                                // vars left unbound).
                                destructureAssignmentPattern(tgt, boxValueIfNeeded(value));
                            }
                            ++index;
                        }
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
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    // closeBlock: break lands here. Iterator-protocol loops call
    // iterator.return() (ES 7.4.8 IteratorClose); the indexed-array path has
    // no iterator, so it is a plain fall-through.
    builder_.setInsertPoint(closeBlock);
    currentBlock_ = closeBlock;
    if (forofCloseIterAlloca) {
        auto closeIter = builder_.createLoad(HIRType::makeObject(), forofCloseIterAlloca);
        builder_.createCall("ts_iterator_close", {closeIter}, HIRType::makeVoid());
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
    LoopContext ctx = {updateBlock, endBlock, withDepth_};
    loopStack_.push(ctx);
    breakTargetStack_.push(endBlock);

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
    if (!myLabel.empty()) {
        labeledLoops_.erase(myLabel);
    }
    popScope();

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
}

void ASTToHIR::visitBreakStatement(ast::BreakStatement* node) {
    setSourceLine(node);
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

    auto* endBlock = createBlock("switch.end");
    switchStack_.push({endBlock, {}, nullptr});
    breakTargetStack_.push(endBlock);

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
    // reading `f` after the switch must throw ReferenceError). Scope state is
    // lexical/compile-time, orthogonal to the basic-block juggling below.
    pushScope();
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

    builder_.setInsertPoint(endBlock);
    currentBlock_ = endBlock;
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

    // Setup try: push handler and call setjmp
    // Returns true if we're coming from an exception, false on normal entry
    auto isException = builder_.createSetupTry(exceptionDest);
    builder_.createCondBranch(isException, exceptionDest, tryBB);

    // --- Try Block ---
    builder_.setInsertPoint(tryBB);
    currentBlock_ = tryBB;

    tryDepth_++;
    tryScopeStack_.push_back({currentFunction_, exceptionDest});
    for (auto& stmt : node->tryBlock) {
        if (hasTerminator()) break;  // Stop if block already terminated (e.g., by throw)
        lowerStatement(stmt.get());
    }
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
