#include "ASTToHIR_Internal.h"

namespace ts::hir {


void ASTToHIR::visitBinaryExpression(ast::BinaryExpression* node) {
    setSourceLine(node);
    const std::string& op = node->op;

    // Logical-assignment operators with short-circuit semantics
    // (ECMA-262 §13.15.2): a ??= b, a ||= b, a &&= b.
    // RHS must NOT evaluate when the assignment is skipped — and the
    // assignment itself must be skipped (not just no-op'd) so that
    // non-configurable properties aren't redefined etc.
    //
    //   a ??= b   →   if (a is nullish) a = b; return a
    //   a ||= b   →   if (a is falsy)   a = b; return a
    //   a &&= b   →   if (a is truthy)  a = b; return a
    if (op == "??=" || op == "||=" || op == "&&=") {
        // 1. Load LHS.
        auto lhs = lowerExpression(node->left.get());
        auto boxedLhs = boxValueIfNeeded(lhs);

        // 2. Compute condition (true = perform assignment).
        std::shared_ptr<HIRValue> shouldAssign;
        if (op == "??=") {
            shouldAssign = builder_.createCall(
                "ts_value_is_nullish", {boxedLhs}, HIRType::makeBool());
        } else {
            // ||= or &&= : need truthiness conversion.
            std::shared_ptr<HIRValue> isTruthy;
            if (lhs->type && lhs->type->kind == HIRTypeKind::Bool) {
                isTruthy = lhs;
            } else {
                isTruthy = builder_.createCall(
                    "ts_value_to_bool", {boxedLhs}, HIRType::makeBool());
            }
            if (op == "&&=") {
                shouldAssign = isTruthy;
            } else {  // ||=
                // shouldAssign = !isTruthy. ts_value_to_bool returns i1;
                // emit XOR with constant true to get NOT.
                auto trueConst = builder_.createConstBool(true);
                shouldAssign = builder_.createXorI64(isTruthy, trueConst);
            }
        }

        // 3. Branch structure.
        int blockId = blockCounter_++;
        auto* assignBlock = builder_.createBlock("lassign_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("lassign_merge_" + std::to_string(blockId));
        auto* lhsBlock = builder_.getInsertBlock();

        builder_.createCondBranch(shouldAssign, assignBlock, mergeBlock);

        // 4. Assign block — lower RHS, store back, fall through to merge.
        builder_.setInsertPoint(assignBlock);
        currentBlock_ = assignBlock;
        auto rhs = lowerExpression(node->right.get());
        auto boxedRhs = boxValueIfNeeded(rhs);

        // Inline store-back. Mirrors the LValue handling in the existing
        // compound-assignment branch below. The three supported LHS forms
        // cover the spec's AssignmentTarget enumeration: identifier,
        // property access, element access. We box the rhs when storing
        // into an Any-typed slot — the LHS variable's existing type is
        // usually Any (since logical-assignment LHS is by definition a
        // value that could be nullish/falsy/etc.), so writing a primitive
        // Int64 directly into a ptr slot produces garbage on readback.
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->left.get())) {
            // boxedRhs is used when the slot is Any/ptr-typed.
            auto storeIntoSlot = [&](std::shared_ptr<HIRValue> slotPtr,
                                     std::shared_ptr<HIRType> slotType) {
                std::shared_ptr<HIRValue> toStore = rhs;
                if (slotType && slotType->kind == HIRTypeKind::Any) {
                    toStore = boxedRhs;
                }
                builder_.createStore(toStore, slotPtr, slotType);
            };
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                if (isCapturedVariable(ident->name, &scopeIdx)) {
                    moduleGlobalsUsedByInnerByModule_[ident->name].insert(currentModulePath_);
                    builder_.createStoreGlobal(modVarName(ident->name), boxedRhs);
                } else if (auto* info = lookupVariableInfo(ident->name)) {
                    if (info->isAlloca) {
                        storeIntoSlot(info->value, info->elemType);
                    }
                    builder_.createStoreGlobal(modVarName(ident->name), boxedRhs);
                }
            } else {
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    auto* capInfo = lookupVariableInfo(ident->name);
                    auto type = capInfo && capInfo->elemType ? capInfo->elemType : rhs->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, boxedRhs);
                } else if (auto* info = lookupVariableInfo(ident->name)) {
                    if (info->isAlloca) {
                        storeIntoSlot(info->value, info->elemType);
                        broadcastCaptureWrite(info, boxedRhs);
                    } else {
                        auto allocaPtr = builder_.createAlloca(rhs->type, ident->name);
                        builder_.createStore(rhs, allocaPtr, rhs->type);
                        info->value = allocaPtr;
                        info->elemType = rhs->type;
                        info->isAlloca = true;
                    }
                }
            }
        } else if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get())) {
            auto obj = lowerExpression(propAccess->expression.get());
            auto propName = builder_.createConstString(propAccess->name);
            // Strict code: a blocked write throws TypeError (PutValue throw=true).
            builder_.createCall(strictCode_ ? "ts_object_set_property_strict"
                                            : "ts_object_set_property",
                {obj, propName, boxedRhs}, HIRType::makeVoid());
        } else if (auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get())) {
            auto arr = lowerExpression(elemAccess->expression.get());
            auto idx = lowerExpression(elemAccess->argumentExpression.get());
            builder_.createCall("ts_array_set",
                {arr, idx, boxedRhs}, HIRType::makeVoid());
        }

        auto* finalAssignBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // 5. Merge — phi between original LHS (skipped path) and RHS (took path).
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalAssignBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

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
        currentBlock_ = rhsBlock;  // Keep ASTToHIR's currentBlock_ in sync
        auto rhs = lowerExpression(node->right.get());
        // Box rhs to Any if needed (for consistent phi node type)
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge with phi node - both values should now be Any/ptr type
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;  // Keep ASTToHIR's currentBlock_ in sync
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    // Handle instanceof operator - need to handle rhs specially as class reference
    if (op == "instanceof") {
        auto lhs = lowerExpression(node->left.get());

        // rhs should be a class identifier - don't evaluate it as an expression
        auto* ident = dynamic_cast<ast::Identifier*>(node->right.get());
        if (ident) {
            // Check for built-in types like Array
            if (ident->name == "Array") {
                // Real arrays match by magic; a `class C extends Array`
                // instance matches via the prototype-chain walk.
                lastValue_ = builder_.createCall("ts_instanceof_array",
                    {boxValueIfNeeded(lhs)}, HIRType::makeBool());
                return;
            }

            // Check if identifier refers to a compiler-known class with a vtable
            bool isKnownClass = false;
            if (module_) {
                for (auto& shape : module_->shapes) {
                    if (shape->className == ident->name) {
                        isKnownClass = true;
                        break;
                    }
                }
            }
            if (isKnownClass) {
                // Known class: use fast vtable comparison
                std::string vtableGlobalName = ident->name + "_VTable_Global";
                auto vtablePtr = builder_.createLoadGlobal(vtableGlobalName);
                lastValue_ = builder_.createInstanceOf(lhs, vtablePtr);
            } else {
                // Unknown class (dynamic constructor, e.g., from require()):
                // use JS-spec prototype-chain instanceof
                auto rhs = lowerExpression(node->right.get());
                auto boxedRhs = boxValueIfNeeded(rhs);
                auto boxedLhs = boxValueIfNeeded(lhs);
                lastValue_ = builder_.createCall("ts_instanceof_dynamic",
                    {boxedLhs, boxedRhs}, HIRType::makeBool());
            }
        } else {
            // RHS is an expression (not a simple identifier) - use dynamic instanceof
            auto rhs = lowerExpression(node->right.get());
            auto boxedRhs = boxValueIfNeeded(rhs);
            auto boxedLhs = boxValueIfNeeded(lhs);
            lastValue_ = builder_.createCall("ts_instanceof_dynamic",
                {boxedLhs, boxedRhs}, HIRType::makeBool());
        }
        return;
    }

    // Handle 'in' operator - check if property exists in object
    if (op == "in") {
        // Ergonomic brand check `#x in obj` (ES2022): the parser synthesizes
        // a StringLiteral marked isPrivateBrand for the LHS. Probe the hidden
        // storage key "\x01#x" (privateStorageKey) — private members are not
        // property keys, and the runtime has_prop deliberately does NOT
        // retry '#' strings (a user-written '"#x" in obj' must stay false
        // for private members).
        if (auto* lhsLit = dynamic_cast<ast::StringLiteral*>(node->left.get());
            lhsLit && lhsLit->isPrivateBrand) {
            auto keyStr = builder_.createConstString(privateStorageKey(lhsLit->value));
            auto rhs = lowerExpression(node->right.get());
            lastValue_ = builder_.createCall("ts_object_has_property",
                {rhs, keyStr}, HIRType::makeBool());
            return;
        }
        auto lhs = lowerExpression(node->left.get());  // property key
        auto rhs = lowerExpression(node->right.get()); // object
        lastValue_ = builder_.createCall("ts_object_has_property", {rhs, lhs}, HIRType::makeBool());
        return;
    }

    // Handle logical AND with short-circuit semantics
    // Must be before general lhs/rhs evaluation to avoid eagerly evaluating RHS
    if (op == "&&") {
        auto lhs = lowerExpression(node->left.get());
        auto boxedLhs = boxValueIfNeeded(lhs);

        // Convert LHS to boolean for branching
        std::shared_ptr<HIRValue> lhsCond;
        if (lhs->type && lhs->type->kind == HIRTypeKind::Bool) {
            lhsCond = lhs;
        } else {
            lhsCond = builder_.createCall("ts_value_to_bool", {boxedLhs}, HIRType::makeBool());
        }

        int blockId = blockCounter_++;
        auto* rhsBlock = builder_.createBlock("land_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("land_merge_" + std::to_string(blockId));
        auto* lhsBlock = builder_.getInsertBlock();

        // && short-circuit: if truthy → eval RHS, if falsy → skip to merge with LHS
        builder_.createCondBranch(lhsCond, rhsBlock, mergeBlock);

        // RHS block
        builder_.setInsertPoint(rhsBlock);
        currentBlock_ = rhsBlock;  // Keep ASTToHIR's currentBlock_ in sync
        auto rhs = lowerExpression(node->right.get());
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Move mergeBlock to end of block list so it's lowered AFTER any blocks
        // created during RHS evaluation (e.g., nested || creates lor_rhs/lor_merge
        // blocks that must be lowered before the merge block's phi can see them
        // as predecessors).
        {
            auto& blocks = currentFunction_->blocks;
            auto it = std::find_if(blocks.begin(), blocks.end(),
                [mergeBlock](const auto& b) { return b.get() == mergeBlock; });
            if (it != blocks.end() && std::next(it) != blocks.end()) {
                std::rotate(it, std::next(it), blocks.end());
            }
        }

        // Merge with phi
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;  // Keep ASTToHIR's currentBlock_ in sync
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    // Handle logical OR with short-circuit semantics
    if (op == "||") {
        auto lhs = lowerExpression(node->left.get());
        auto boxedLhs = boxValueIfNeeded(lhs);

        // Convert LHS to boolean for branching
        std::shared_ptr<HIRValue> lhsCond;
        if (lhs->type && lhs->type->kind == HIRTypeKind::Bool) {
            lhsCond = lhs;
        } else {
            lhsCond = builder_.createCall("ts_value_to_bool", {boxedLhs}, HIRType::makeBool());
        }

        int blockId = blockCounter_++;
        auto* rhsBlock = builder_.createBlock("lor_rhs_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("lor_merge_" + std::to_string(blockId));
        auto* lhsBlock = builder_.getInsertBlock();

        // || short-circuit: if truthy → skip to merge with LHS, if falsy → eval RHS
        builder_.createCondBranch(lhsCond, mergeBlock, rhsBlock);

        // RHS block
        builder_.setInsertPoint(rhsBlock);
        currentBlock_ = rhsBlock;  // Keep ASTToHIR's currentBlock_ in sync
        auto rhs = lowerExpression(node->right.get());
        auto boxedRhs = boxValueIfNeeded(rhs);
        auto* finalRhsBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Move mergeBlock to end of block list (same reason as && above)
        {
            auto& blocks = currentFunction_->blocks;
            auto it = std::find_if(blocks.begin(), blocks.end(),
                [mergeBlock](const auto& b) { return b.get() == mergeBlock; });
            if (it != blocks.end() && std::next(it) != blocks.end()) {
                std::rotate(it, std::next(it), blocks.end());
            }
        }

        // Merge with phi
        builder_.setInsertPoint(mergeBlock);
        currentBlock_ = mergeBlock;  // Keep ASTToHIR's currentBlock_ in sync
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(boxedLhs, lhsBlock));
        phiIncoming.push_back(std::make_pair(boxedRhs, finalRhsBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    auto lhs = lowerExpression(node->left.get());
    auto rhs = lowerExpression(node->right.get());

    // Strategy B Phase 4c: AST fallback removed.
    //
    // Until Phase 4a, these helpers had to fall back to `astNode->inferredType`
    // because GetPropStatic for typed property access emitted with type=Any,
    // losing the analyzer's type info on the HIR side. Phase 4a fixed that:
    // ASTToHIR now passes the class-shape-derived type to createGetPropStatic,
    // and the LLVM value at the SSA name is the actual typed thing.
    //
    // The AST fallback is now redundant and removed. The Phase 0b probe
    // (commit caa81b8) regressed `array_churn` and `linked_list` by 36-46%
    // because of the missing type info; that regression should NOT recur
    // after 4a + 4b.
    //
    // BigInt is the only type still keyed off `astNode->inferredType` because
    // HIRTypeKind::BigInt isn't yet propagated through HIRValue::type.
    auto isString = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::String;
    };

    auto isFloat64 = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::Float64;
    };

    auto isBigInt = [](ast::Expression* astNode) {
        if (!astNode) return false;
        // BigInt literal (e.g. `1n`) — trust the syntactic tag even if the
        // analyzer didn't run (untyped JS relaxed mode).
        if (dynamic_cast<ast::BigIntLiteral*>(astNode)) return true;
        // Otherwise rely on inferredType from the analyzer.
        return astNode->inferredType &&
               astNode->inferredType->kind == ts::TypeKind::BigInt;
    };

    auto isNumber = [&isFloat64](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        if (val && val->type) {
            if (val->type->kind == HIRTypeKind::Int64 ||
                val->type->kind == HIRTypeKind::Float64) return true;
        }
        return false;
    };

    auto isBoolean = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::Bool;
    };

    auto isAnyOrNullish = [](const std::shared_ptr<HIRValue>& val, ast::Expression*) {
        return val && val->type && val->type->kind == HIRTypeKind::Any;
    };

    // Helper to check if an expression is the literal `undefined` keyword
    auto isUndefinedLiteral = [](ast::Expression* astNode) {
        if (auto* id = dynamic_cast<ast::Identifier*>(astNode)) {
            return id->name == "undefined";
        }
        return false;
    };

    // Helper to check if an expression is the literal `null` keyword
    auto isNullLiteral = [](ast::Expression* astNode) {
        if (auto* nullLit = dynamic_cast<ast::NullLiteral*>(astNode)) {
            return true;
        }
        if (auto* id = dynamic_cast<ast::Identifier*>(astNode)) {
            return id->name == "null";
        }
        return false;
    };

    // For strict equality (===), check if types are incompatible
    // Returns true if types are definitely different and === should return false
    auto typesIncompatibleForStrictEqual = [&isString, &isNumber, &isBoolean, &isBigInt](
            const std::shared_ptr<HIRValue>& lhsVal, ast::Expression* lhsAst,
            const std::shared_ptr<HIRValue>& rhsVal, ast::Expression* rhsAst) {
        bool lhsIsString = isString(lhsVal, lhsAst);
        bool rhsIsString = isString(rhsVal, rhsAst);
        bool lhsIsNumber = !lhsIsString && ((lhsVal && lhsVal->type &&
            (lhsVal->type->kind == HIRTypeKind::Int64 || lhsVal->type->kind == HIRTypeKind::Float64))
            || (lhsAst && lhsAst->inferredType &&
            (lhsAst->inferredType->kind == ts::TypeKind::Int ||
             lhsAst->inferredType->kind == ts::TypeKind::Double)));
        bool rhsIsNumber = !rhsIsString && ((rhsVal && rhsVal->type &&
            (rhsVal->type->kind == HIRTypeKind::Int64 || rhsVal->type->kind == HIRTypeKind::Float64))
            || (rhsAst && rhsAst->inferredType &&
            (rhsAst->inferredType->kind == ts::TypeKind::Int ||
             rhsAst->inferredType->kind == ts::TypeKind::Double)));
        bool lhsIsBoolean = isBoolean(lhsVal, lhsAst);
        bool rhsIsBoolean = isBoolean(rhsVal, rhsAst);
        bool lhsIsBigInt = isBigInt(lhsAst);
        bool rhsIsBigInt = isBigInt(rhsAst);

        // If both are the same type category, compatible
        if (lhsIsString && rhsIsString) return false;
        if (lhsIsNumber && rhsIsNumber) return false;
        if (lhsIsBoolean && rhsIsBoolean) return false;
        if (lhsIsBigInt && rhsIsBigInt) return false;

        // If one has a known type and the other has a different known type, incompatible
        if (lhsIsString && (rhsIsNumber || rhsIsBoolean || rhsIsBigInt)) return true;
        if (lhsIsNumber && (rhsIsString || rhsIsBoolean || rhsIsBigInt)) return true;
        if (lhsIsBoolean && (rhsIsString || rhsIsNumber || rhsIsBigInt)) return true;
        if (lhsIsBigInt && (rhsIsString || rhsIsNumber || rhsIsBoolean)) return true;

        // If types are unknown (Any), can't determine incompatibility at compile time
        return false;
    };

    // Determine if we should use Float64 operations (if either operand is Float64)
    bool useFloat = isFloat64(lhs, node->left.get()) || isFloat64(rhs, node->right.get());
    // BigInt: check HIRValue type (set by visitBigIntLiteral and propagated
    // through variable references) first, then fall back to the ast-level tag.
    // Require BOTH operands to be known BigInt — mixed BigInt/Number is a
    // TypeError per spec and must not hit the bigint-only fast path
    // (which would type-mismatch ts_bigint_add/gt against a raw double).
    auto hirIsBigInt = [](const std::shared_ptr<HIRValue>& v) {
        return v && v->type && v->type->kind == HIRTypeKind::BigInt;
    };
    bool lhsIsBigInt = hirIsBigInt(lhs) || isBigInt(node->left.get());
    bool rhsIsBigInt = hirIsBigInt(rhs) || isBigInt(node->right.get());
    bool useBigInt = lhsIsBigInt && rhsIsBigInt;

    // Coercion-routing categories (ECMA-262 13.15.3
    // ApplyStringOrNumericBinaryOperator). The typed fast paths only handle
    // statically numeric (and matching bool/bool) operands; everything else
    // must reach the coercing ts_value_* runtime dispatchers or wrong results
    // appear: true + 1 -> 0 (bool fed to raw i64 math), new Boolean(true) +
    // true -> 1 (valueOf never invoked), anyStr + 1 -> NaN instead of "x1".
    //   0 = numeric (Int64/Float64)  1 = string  2 = Any  3 = bool
    //   4 = other (object/class, null/undefined, BigInt-typed, function...)
    auto typeCat = [&](const std::shared_ptr<HIRValue>& v, ast::Expression* a) -> int {
        if (isNumber(v, a)) return 0;
        if (isString(v, a)) return 1;
        if (isAnyOrNullish(v, a)) return 2;
        if (isBoolean(v, a)) return 3;
        return 4;
    };
    int lcat = typeCat(lhs, node->left.get());
    int rcat = typeCat(rhs, node->right.get());
    // An object/wrapper/null-ish operand always needs the runtime path.
    bool eitherOther = (lcat == 4 || rcat == 4);
    // A bool mixed with a non-bool corrupts the raw i64 fast path; a
    // bool/bool pair is handled correctly by the existing typed lowering.
    bool boolMix = (lcat == 3) != (rcat == 3);
    // Any mixed with a statically-typed number: the generic opcode wrongly
    // specializes to numeric math (probe: any "x" + 1 -> NaN, not "x1").
    bool anyNumMix = (lcat == 2 && rcat == 0) || (lcat == 0 && rcat == 2);
    // Combined predicate for the binary arithmetic/comparison forms.
    bool needsCoercion = eitherOther || boolMix || anyNumMix;

    if (op == "+") {
        // Coercing runtime path — but keep the string-concat fast path
        // (either side statically String) which already handles dynamic
        // operands via SpecializationPass.
        // A mixed BigInt operand routes even when the other side is a string:
        // StringConcat cannot stringify a raw TsBigInt ("" + 1n crashed), while
        // ts_value_add handles ToString(BigInt) concat and the mix TypeError.
        if (!useBigInt && ((lcat != 1 && rcat != 1 && needsCoercion) ||
                           lhsIsBigInt || rhsIsBigInt)) {
            auto lb = boxValueIfNeeded(lhs);
            auto rb = boxValueIfNeeded(rhs);
            lastValue_ = builder_.createCall("ts_value_add", {lb, rb}, HIRType::makeAny());
            return;
        }
        // Strategy B Phase 3: emit generic Add. SpecializationPass (which
        // runs after TypePropagationPass) will rewrite this into the
        // appropriate type-specific instruction (StringConcat, AddF64,
        // AddI64, ts_bigint_add, ts_value_add) based on operand types.
        //
        // We still use the AST-fallback helpers here to compute a precise
        // best-guess result type for the generic instruction. This is the
        // load-bearing AST fallback identified in the Phase 0b probe — it
        // can be removed once Phase 4 fixes GetPropStatic precision and
        // SpecializationPass has access to all the same type info.
        std::shared_ptr<HIRType> resultType;
        if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
            resultType = HIRType::makeString();
        } else if (useBigInt) {
            resultType = HIRType::makeBigInt();
        } else if (isAnyOrNullish(lhs, node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            resultType = HIRType::makeAny();
        } else if (useFloat) {
            resultType = HIRType::makeFloat64();
        } else {
            resultType = HIRType::makeInt64();
        }
        lastValue_ = builder_.createAdd(lhs, rhs, resultType);
    } else if (op == "-" || op == "*" || op == "/" || op == "%") {
        // Strategy B Phase 3: emit generic Sub/Mul/Div/Mod. SpecializationPass
        // will rewrite to the type-specific opcode based on operand types.
        // Note these operators (unlike +) use OR for the Any-fallback check —
        // if either operand is Any, dispatch dynamically.
        // String operands (e.g. "1"/"1") must ToNumber-coerce and use IEEE-754
        // arithmetic. The generic Int64 fast path would unbox each string to 0
        // and emit sdiv/srem, which TRAP on the zero divisor
        // (STATUS_INTEGER_DIVIDE_BY_ZERO) — and yields a wrong 0 for "6"-"3".
        // Route string operands to the dynamic Any path (the coercing
        // ts_value_sub/mul/div/mod runtime helpers, which ToNumber-coerce and
        // never trap: 0/0 -> NaN), exactly as the `+` operator routes to
        // StringConcat. ECMA-262 13.7: multiplicative operators are IEEE-754.
        bool eitherString = isString(lhs, node->left.get()) ||
                            isString(rhs, node->right.get());
        // Coercion routing (see typeCat above): booleans/objects/wrappers and
        // an Any/typed-number mix must also reach the coercing helpers — the
        // raw i64/f64 ops corrupt them (true - 1, new Number(2) * 3, ...).
        if (!useBigInt && (eitherString || needsCoercion)) {
            // Box both operands and call the coercing runtime helper directly.
            // The generic Sub/Div/Mod opcodes specialize on OPERAND types, and
            // String operands have no typed arithmetic form — they fell to the
            // Int64 path (sdiv/srem on a string-unboxed 0 → TRAP) or returned
            // undefined. ts_value_{sub,mul,div,mod} ToNumber-coerce and use
            // IEEE-754 (0/0 -> NaN), matching ECMA-262 13.7.
            const char* fn = (op == "-") ? "ts_value_sub"
                           : (op == "*") ? "ts_value_mul"
                           : (op == "/") ? "ts_value_div"
                           :               "ts_value_mod";
            auto lb = boxValueIfNeeded(lhs);
            auto rb = boxValueIfNeeded(rhs);
            lastValue_ = builder_.createCall(fn, {lb, rb}, HIRType::makeAny());
        } else {
            std::shared_ptr<HIRType> resultType;
            if (useBigInt) {
                resultType = HIRType::makeBigInt();
            } else if (isAnyOrNullish(lhs, node->left.get()) ||
                       isAnyOrNullish(rhs, node->right.get())) {
                resultType = HIRType::makeAny();
            } else if (useFloat) {
                resultType = HIRType::makeFloat64();
            } else {
                resultType = HIRType::makeInt64();
            }
            if (op == "-")      lastValue_ = builder_.createSub(lhs, rhs, resultType);
            else if (op == "*") lastValue_ = builder_.createMul(lhs, rhs, resultType);
            else if (op == "/") lastValue_ = builder_.createDiv(lhs, rhs, resultType);
            else                lastValue_ = builder_.createMod(lhs, rhs, resultType);
        }
    } else if (op == "**") {
        // Exponentiation. No specialized HIR opcode; dispatch directly:
        // - BigInt ** BigInt → ts_bigint_pow (arbitrary-precision integer).
        // - Numeric           → ts_math_pow (double, matches Math.pow).
        // Mixed BigInt/Number is a TypeError per spec; we route through
        // ts_math_pow which will coerce the BigInt to NaN (approximate).
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_pow", {lhs, rhs}, HIRType::makeBigInt());
        } else if (lcat != 0 || rcat != 0) {
            // Non-numeric operand(s): ts_value_pow implements the full
            // ApplyStringOrNumericBinaryOperator (ToPrimitive/ToNumber, BigInt
            // pair -> ts_bigint_pow, BigInt/other mix -> TypeError).
            auto lb = boxValueIfNeeded(lhs);
            auto rb = boxValueIfNeeded(rhs);
            lastValue_ = builder_.createCall("ts_value_pow", {lb, rb}, HIRType::makeAny());
        } else {
            // Ensure both operands are Float64 for ts_math_pow.
            auto castToF64 = [this](std::shared_ptr<HIRValue> v) {
                if (v && v->type) {
                    if (v->type->kind == HIRTypeKind::Int64) return builder_.createCastI64ToF64(v);
                    if (v->type->kind == HIRTypeKind::Float64) return v;
                }
                return v;
            };
            auto lhsF = castToF64(lhs);
            auto rhsF = castToF64(rhs);
            lastValue_ = builder_.createCall("ts_math_pow", {lhsF, rhsF}, HIRType::makeFloat64());
        }
    } else if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        // Strategy B Phase 4d: emit generic ordering comparison.
        // After Phase 4a+4c, operand types are reliable in HIR, so
        // SpecializationPass can pick the right typed form from
        // operand val->type alone (no AST fallback needed).
        //
        // Equality forms (==, !=, ===, !==) are NOT migrated here —
        // they have many special cases (typesIncompatibleForStrictEqual,
        // isString-pair check, isUndefinedLiteral/isNullLiteral) that are
        // language-feature handling, not pure type-driven specialization.
        if (useBigInt) {
            const char* fn = (op == "<")  ? "ts_bigint_lt"
                           : (op == "<=") ? "ts_bigint_le"
                           : (op == ">")  ? "ts_bigint_gt"
                           :                "ts_bigint_ge";
            lastValue_ = builder_.createCall(fn, {lhs, rhs}, HIRType::makeBool());
        } else if (eitherOther || boolMix || (lcat == 1 && rcat == 1)) {
            // Mixed booleans, wrapper objects (ToPrimitive/valueOf), and mixed
            // BigInt/Number (allowed by ES 7.2.12, handled in the runtime
            // comparators) — the typed CmpLt forms corrupt these operands.
            // Statically-String pairs route too: the generic CmpLt compared
            // raw pointers ("a" < "b" was false BOTH ways); ts_value_lt does
            // the spec lexicographic comparison.
            const char* fn = (op == "<")  ? "ts_value_lt"
                           : (op == "<=") ? "ts_value_lte"
                           : (op == ">")  ? "ts_value_gt"
                           :                "ts_value_gte";
            auto lb = boxValueIfNeeded(lhs);
            auto rb = boxValueIfNeeded(rhs);
            lastValue_ = builder_.createCall(fn, {lb, rb}, HIRType::makeAny());
        } else {
            std::shared_ptr<HIRValue> v;
            if      (op == "<")  v = builder_.createCmpLt(lhs, rhs);
            else if (op == "<=") v = builder_.createCmpLe(lhs, rhs);
            else if (op == ">")  v = builder_.createCmpGt(lhs, rhs);
            else                 v = builder_.createCmpGe(lhs, rhs);
            lastValue_ = v;
        }
    } else if (op == "==") {
        // Loose equality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (lhsIsBigInt || rhsIsBigInt) {
            // Mixed BigInt/Number/String: route through ts_value_eq which
            // handles BigInt↔Number/String value comparison per spec.
            // Emitting CmpEqF64 here would unbox the BigInt ptr as double → garbage.
            lastValue_ = builder_.createCall("ts_value_eq", {lhs, rhs}, HIRType::makeAny());
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get()) ||
                   eitherOther || boolMix) {
            // eitherOther/boolMix: objects and mixed booleans need the
            // coercing comparison too (`new Number(1) == 1`, `"1" == true`) —
            // CmpEqI64 on a pointer or raw i1 is garbage. Box for the call.
            lastValue_ = builder_.createCall("ts_value_eq",
                {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
        }
    } else if (op == "===") {
        // Strict equality - if types are incompatible, return false directly
        if (typesIncompatibleForStrictEqual(lhs, node->left.get(), rhs, node->right.get())) {
            lastValue_ = builder_.createConstBool(false);
        } else if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isString(lhs, node->left.get()) && isString(rhs, node->right.get())) {
            // String comparison using ts_string_eq
            lastValue_ = builder_.createCall("ts_string_eq", {lhs, rhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x === undefined where x is Any type: use ts_value_is_undefined(x)
            // This correctly checks if a TsValue* has type == UNDEFINED
            lastValue_ = builder_.createCall("ts_value_is_undefined", {lhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // undefined === x where x is Any type: use ts_value_is_undefined(x)
            lastValue_ = builder_.createCall("ts_value_is_undefined", {rhs}, HIRType::makeBool());
        } else if (isNullLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x === null where x is Any type: use ts_value_is_null(x)
            lastValue_ = builder_.createCall("ts_value_is_null", {lhs}, HIRType::makeBool());
        } else if (isNullLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // null === x where x is Any type: use ts_value_is_null(x)
            lastValue_ = builder_.createCall("ts_value_is_null", {rhs}, HIRType::makeBool());
        } else if (isUndefinedLiteral(node->left.get()) && isUndefinedLiteral(node->right.get())) {
            // undefined === undefined is always true
            lastValue_ = builder_.createConstBool(true);
        } else if (isNullLiteral(node->left.get()) && isNullLiteral(node->right.get())) {
            // null === null is always true
            lastValue_ = builder_.createConstBool(true);
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            // When either operand is Any type, use runtime strict equality which
            // checks types first (e.g., undefined === true must be false, not coerced)
            // Return boxed TsValue* to preserve ptr typing for variables that may be
            // reassigned later with non-boolean values (e.g., match = regex.exec(...))
            lastValue_ = builder_.createCall("ts_value_strict_eq", {lhs, rhs}, HIRType::makeAny());
        } else {
            lastValue_ = useFloat ? builder_.createCmpEqF64(lhs, rhs) : builder_.createCmpEqI64(lhs, rhs);
        }
    } else if (op == "!=") {
        // Loose inequality - use coercing comparison
        if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ne", {lhs, rhs}, HIRType::makeBool());
        } else if (lhsIsBigInt || rhsIsBigInt) {
            // Mixed: route through ts_value_eq + negate (same reasoning as ==).
            auto eq = builder_.createCall("ts_value_eq", {lhs, rhs}, HIRType::makeAny());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get()) ||
                   eitherOther || boolMix) {
            // Use ts_value_eq and negate; eitherOther/boolMix mirrors `==`.
            auto eq = builder_.createCall("ts_value_eq",
                {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            lastValue_ = builder_.createLogicalNot(eq);
        } else {
            lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
        }
    } else if (op == "!==") {
        // Strict inequality - if types are incompatible, return true directly
        if (typesIncompatibleForStrictEqual(lhs, node->left.get(), rhs, node->right.get())) {
            lastValue_ = builder_.createConstBool(true);
        } else if (useBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_ne", {lhs, rhs}, HIRType::makeBool());
        } else if (isString(lhs, node->left.get()) && isString(rhs, node->right.get())) {
            // String comparison using ts_string_eq, then negate
            auto eq = builder_.createCall("ts_string_eq", {lhs, rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x !== undefined where x is Any type: negate ts_value_is_undefined(x)
            auto eq = builder_.createCall("ts_value_is_undefined", {lhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // undefined !== x where x is Any type: negate ts_value_is_undefined(x)
            auto eq = builder_.createCall("ts_value_is_undefined", {rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isNullLiteral(node->right.get()) && isAnyOrNullish(lhs, node->left.get())) {
            // x !== null where x is Any type: negate ts_value_is_null(x)
            auto eq = builder_.createCall("ts_value_is_null", {lhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isNullLiteral(node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
            // null !== x where x is Any type: negate ts_value_is_null(x)
            auto eq = builder_.createCall("ts_value_is_null", {rhs}, HIRType::makeBool());
            lastValue_ = builder_.createLogicalNot(eq);
        } else if (isUndefinedLiteral(node->left.get()) && isUndefinedLiteral(node->right.get())) {
            // undefined !== undefined is always false
            lastValue_ = builder_.createConstBool(false);
        } else if (isNullLiteral(node->left.get()) && isNullLiteral(node->right.get())) {
            // null !== null is always false
            lastValue_ = builder_.createConstBool(false);
        } else if (isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())) {
            // When either operand is Any type, use runtime strict equality and negate
            // Return boxed TsValue* to preserve ptr typing
            auto eq = builder_.createCall("ts_value_strict_eq", {lhs, rhs}, HIRType::makeAny());
            lastValue_ = builder_.createLogicalNot(eq);
        } else {
            lastValue_ = useFloat ? builder_.createCmpNeF64(lhs, rhs) : builder_.createCmpNeI64(lhs, rhs);
        }
    } else if (op == "&&") {
        lastValue_ = builder_.createLogicalAnd(lhs, rhs);
    } else if (op == "||") {
        lastValue_ = builder_.createLogicalOr(lhs, rhs);
    } else if (op == "&" || op == "|" || op == "^" ||
               op == "<<" || op == ">>" || op == ">>>") {
        // Bitwise / shift. The raw *I64 forms are only valid for statically
        // numeric operands — strings/bools/objects/Any/BigInt must go through
        // the coercing runtime dispatchers (ES ToInt32/ToUint32; both-BigInt
        // uses the BigInt variants; a BigInt/other mix throws TypeError).
        // Previously these ALWAYS emitted the raw i64 op, so `"x" ^ "1"`,
        // `new Number(3) & 1`, and `1n << 1n` were corrupted.
        if (lcat != 0 || rcat != 0) {
            const char* fn = (op == "&")  ? "ts_value_and"
                           : (op == "|")  ? "ts_value_or"
                           : (op == "^")  ? "ts_value_xor"
                           : (op == "<<") ? "ts_value_shl"
                           : (op == ">>") ? "ts_value_sar"
                           :                "ts_value_ushr";
            auto lb = boxValueIfNeeded(lhs);
            auto rb = boxValueIfNeeded(rhs);
            lastValue_ = builder_.createCall(fn, {lb, rb}, HIRType::makeAny());
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
        } else {
            lastValue_ = builder_.createUShrI64(lhs, rhs);
        }
    } else if (op == ",") {
        // Comma operator: evaluate both sides for side effects, return right
        // lhs is already evaluated above, rhs is already evaluated above
        lastValue_ = rhs;
    } else if (op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
               op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=" || op == ">>>=") {
        // Compound assignment operators
        // lhs already contains the loaded current value
        // rhs contains the value to add/subtract/etc.
        // We need to:
        // 1. Compute the new value
        // 2. Store it back to the LHS location
        // 3. Return the new value

        std::shared_ptr<HIRValue> result;

        // Compute the operation. eitherAny routes to the coercing runtime
        // helpers; needsCoercion (see typeCat above) extends that to mixed
        // booleans, objects/wrappers, and Any/number mixes, mirroring the
        // binary operator forms (`x op= y` must lower like `x = x op y`).
        bool eitherAny = isAnyOrNullish(lhs, node->left.get()) || isAnyOrNullish(rhs, node->right.get())
                         || needsCoercion;
        if (op == "+=") {
            // Mirror the binary `+` path exactly so that `x += y` lowers
            // identically to `x = x + y`. Emitting a low-level StringConcat
            // directly here is wrong when an operand is Any/boxed (e.g.
            // `arr[i] += ''` where arr[i] is a dynamic value): StringConcat
            // assumes string operands and reads a boxed number as a string
            // pointer → garbage. The generic Add goes through
            // SpecializationPass, which routes Any+String through the runtime
            // coercing add (ts_value_add) just like the binary `+` operator.
            std::shared_ptr<HIRType> resultType;
            if (!useBigInt && lcat != 1 && rcat != 1 && needsCoercion) {
                // Coercing runtime path — mirrors the binary `+` routing.
                result = builder_.createCall("ts_value_add",
                    {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            } else {
                if (isString(lhs, node->left.get()) || isString(rhs, node->right.get())) {
                    resultType = HIRType::makeString();
                } else if (useBigInt) {
                    resultType = HIRType::makeBigInt();
                } else if (isAnyOrNullish(lhs, node->left.get()) && isAnyOrNullish(rhs, node->right.get())) {
                    resultType = HIRType::makeAny();
                } else if (useFloat) {
                    resultType = HIRType::makeFloat64();
                } else {
                    resultType = HIRType::makeInt64();
                }
                result = builder_.createAdd(lhs, rhs, resultType);
            }
        } else if (op == "-=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_sub", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_sub",
                    {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createSubF64(lhs, rhs);
            } else {
                result = builder_.createSubI64(lhs, rhs);
            }
        } else if (op == "*=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_mul", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_mul",
                    {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createMulF64(lhs, rhs);
            } else {
                result = builder_.createMulI64(lhs, rhs);
            }
        } else if (op == "/=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_div", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_div",
                    {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createDivF64(lhs, rhs);
            } else {
                result = builder_.createDivI64(lhs, rhs);
            }
        } else if (op == "%=") {
            if (useBigInt) {
                result = builder_.createCall("ts_bigint_mod", {lhs, rhs}, HIRType::makeObject());
            } else if (eitherAny) {
                result = builder_.createCall("ts_value_mod",
                    {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            } else if (useFloat) {
                result = builder_.createModF64(lhs, rhs);
            } else {
                result = builder_.createModI64(lhs, rhs);
            }
        } else if (op == "&=" || op == "|=" || op == "^=" ||
                   op == "<<=" || op == ">>=" || op == ">>>=") {
            // Mirror the binary bitwise/shift routing: non-numeric operands
            // (string/bool/object/Any/BigInt) go through the coercing runtime
            // dispatchers; numeric pairs keep the raw i64 fast path.
            if (lcat != 0 || rcat != 0) {
                const char* fn = (op == "&=")  ? "ts_value_and"
                               : (op == "|=")  ? "ts_value_or"
                               : (op == "^=")  ? "ts_value_xor"
                               : (op == "<<=") ? "ts_value_shl"
                               : (op == ">>=") ? "ts_value_sar"
                               :                 "ts_value_ushr";
                result = builder_.createCall(fn,
                    {boxValueIfNeeded(lhs), boxValueIfNeeded(rhs)}, HIRType::makeAny());
            } else if (op == "&=") {
                result = builder_.createAndI64(lhs, rhs);
            } else if (op == "|=") {
                result = builder_.createOrI64(lhs, rhs);
            } else if (op == "^=") {
                result = builder_.createXorI64(lhs, rhs);
            } else if (op == "<<=") {
                result = builder_.createShlI64(lhs, rhs);
            } else if (op == ">>=") {
                result = builder_.createShrI64(lhs, rhs);
            } else {
                result = builder_.createUShrI64(lhs, rhs);
            }
        }

        // Now store the result back to the LHS
        // Handle identifier LHS
        auto* ident = dynamic_cast<ast::Identifier*>(node->left.get());
        if (ident) {
            // Inside a `with` scope: the write consults the runtime
            // with-stack (ES 9.1.1.2.5 SetMutableBinding, incl. the strict
            // re-validation ReferenceError). ts_with_ref snapshots which
            // with-object holds the binding NOW (post-read; the compound
            // read already went through the with-aware resolver).
            if (withScopeActive()) {
                auto nameC = builder_.createConstString(ident->name);
                auto ref = builder_.createCall("ts_with_ref", {nameC}, HIRType::makeAny());
                auto strictC = builder_.createConstInt(strictCode_ ? 1 : 0);
                auto wrote = builder_.createCall("ts_with_set_ref_s",
                    {ref, nameC, boxValueIfNeeded(result), strictC}, HIRType::makeAny());
                auto* info0 = lookupVariableInfo(ident->name);
                if (!info0 && !isModuleGlobalVar(ident->name)) {
                    // No static binding at all: with-object or (strict ->
                    // ReferenceError / sloppy -> implicit global).
                    builder_.createCall("ts_with_unref_fallback_set",
                        {wrote, nameC, boxValueIfNeeded(result), strictC},
                        HIRType::makeVoid());
                    lastValue_ = result;
                    return;
                }
                // Static binding exists: if a with-object took the write,
                // skip the static store; else fall through to it.
                int bid = blockCounter_++;
                auto* storeBB = createBlock("withcmp.store" + std::to_string(bid));
                auto* contBB = createBlock("withcmp.cont" + std::to_string(bid));
                builder_.createCondBranch(wrote, contBB, storeBB);
                builder_.setInsertPoint(storeBB);
                currentBlock_ = storeBB;
                // (fall through to the normal store paths below, then jump)
                // Emit the static store inline: reuse the alloca path.
                auto* infoS = lookupVariableInfo(ident->name);
                if (infoS && infoS->isAlloca) {
                    auto sv = result;
                    if (result->type && result->type->kind == HIRTypeKind::Any &&
                        infoS->elemType && infoS->elemType->kind != HIRTypeKind::Any) {
                        infoS->elemType = HIRType::makeAny();
                    }
                    builder_.createStore(sv, infoS->value, infoS->elemType);
                    broadcastCaptureWrite(infoS, sv);
                }
                if (isModuleGlobalVar(ident->name)) {
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                }
                builder_.createBranch(contBB);
                builder_.setInsertPoint(contBB);
                currentBlock_ = contBB;
                lastValue_ = result;
                return;
            }
            // For module-scoped variables from inner functions, use __modvar_ globals
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                if (isCapturedVariable(ident->name, &scopeIdx)) {
                    // Mark as used-by-inner so reads in __module_init take the
                    // global path instead of the stale local fast-path alloca.
                    moduleGlobalsUsedByInnerByModule_[ident->name].insert(currentModulePath_);
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                    lastValue_ = result;
                    return;
                }
            }

            // Check if this is a captured variable from an outer function
            {
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    auto* capInfo = lookupVariableInfo(ident->name);
                    auto type = capInfo && capInfo->elemType ? capInfo->elemType : result->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, result);
                    lastValue_ = result;
                    return;
                }
            }

            auto* info = lookupVariableInfo(ident->name);
            if (info && info->isAlloca) {
                // A coercing compound assign returns a boxed Any. NUMERIC
                // slots must keep their declared type: loads inside a loop
                // body were emitted with the old type, so widening the slot
                // mid-body corrupts iteration 2 (`for (const x of [1,2,3])
                // s += x` read the boxed ptr as raw i64 -> 6.375). Unbox the
                // result back to the slot type instead. Non-numeric slots
                // (String etc.) genuinely change VALUE type (`var x = "x";
                // x ^= "1"` leaves x holding the NUMBER 1) — widen those to
                // Any so later reads unbox instead of trusting the stale
                // declared type (`x === 1` would constant-fold false).
                auto storeVal = result;
                if (result->type && result->type->kind == HIRTypeKind::Any &&
                    info->elemType && info->elemType->kind != HIRTypeKind::Any) {
                    if (info->elemType->kind == HIRTypeKind::Int64) {
                        storeVal = builder_.createUnboxInt(result);
                    } else if (info->elemType->kind == HIRTypeKind::Float64) {
                        storeVal = builder_.createUnboxFloat(result);
                    } else {
                        info->elemType = HIRType::makeAny();
                    }
                }
                builder_.createStore(storeVal, info->value, info->elemType);
                broadcastCaptureWrite(info, storeVal);
            } else if (info) {
                // Direct value - promote to alloca for mutability
                auto allocaPtr = builder_.createAlloca(result->type, ident->name);
                builder_.createStore(result, allocaPtr, result->type);
                info->value = allocaPtr;
                info->elemType = result->type;
                info->isAlloca = true;
            }

            // If this variable is a module-scoped global, also update __modvar_ global
            if (isModuleGlobalVar(ident->name)) {
                builder_.createStoreGlobal(modVarName(ident->name), result);
            }

            lastValue_ = result;
            return;
        }

        // Handle property access LHS (e.g., obj.prop += val)
        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->left.get());
        if (propAccess) {
            auto obj = lowerExpression(propAccess->expression.get());
            // Mirror the simple-assignment store: a class setter (including
            // a private accessor `set #x`) must be CALLED; only plain data
            // properties go through set_property — and private FIELDS live
            // under the hidden storage key "\x01#x" (privateStorageKey).
            // Writing the visible "#x" left the real slot stale (the whole
            // private-reference compound-assignment family), while writing
            // the hidden key unconditionally bypassed private setters.
            HIRClass* targetClass = nullptr;
            if (propAccess->expression && propAccess->expression->inferredType) {
                auto exprType = propAccess->expression->inferredType;
                if (exprType->kind == ts::TypeKind::Class) {
                    if (auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType)) {
                        for (auto& cls : module_->classes) {
                            if (cls->name == classType->name) { targetClass = cls.get(); break; }
                        }
                    }
                }
            }
            if (!targetClass) {
                auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
                if (thisIdent && thisIdent->name == "this" && currentClass_) {
                    targetClass = currentClass_;
                }
            }
            if (targetClass) {
                auto setterIt = targetClass->methods.find("__setter_" + propAccess->name);
                if (setterIt != targetClass->methods.end() && setterIt->second) {
                    builder_.createCall(setterIt->second->name,
                                        {obj, boxValueIfNeeded(result)},
                                        HIRType::makeVoid());
                    lastValue_ = result;
                    return;
                }
            }
            const std::string& n = propAccess->name;
            auto propName = builder_.createConstString(
                (!n.empty() && n[0] == '#') ? privateStorageKey(n) : n);
            std::vector<std::shared_ptr<HIRValue>> args = {obj, propName, boxValueIfNeeded(result)};
            // Strict code: a blocked write throws TypeError (PutValue throw=true).
            builder_.createCall(strictCode_ ? "ts_object_set_property_strict"
                                            : "ts_object_set_property",
                args, HIRType::makeVoid());
            lastValue_ = result;
            return;
        }

        // Handle element access LHS (e.g., arr[i] += val)
        auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->left.get());
        if (elemAccess) {
            auto arr = lowerExpression(elemAccess->expression.get());
            auto idx = lowerExpression(elemAccess->argumentExpression.get());
            // Use createSetElem (same as the simple-assignment path in
            // visitAssignmentExpression) so the value is boxed correctly by
            // type. The previous manual `ts_array_set` + boxValueIfNeeded(result)
            // wrapped a String result via ts_value_make_object → a TsString
            // stored as a generic object, so readback/typeof was wrong
            // (e.g. `arr[i] += ''` produced undefined instead of the string).
            builder_.createSetElem(arr, idx, result);
            lastValue_ = result;
            return;
        }

        // Fallback - just return the computed value
        lastValue_ = result;
    } else {
        // Unknown operator - return lhs
        lastValue_ = lhs;
    }
}

void ASTToHIR::visitConditionalExpression(ast::ConditionalExpression* node) {
    setSourceLine(node);
    auto cond = lowerExpression(node->condition.get());

    // Use branch-based evaluation for correct short-circuit semantics.
    // JavaScript's ternary operator must NOT eagerly evaluate both branches
    // because they may have side effects (function calls, property access, etc.)
    int blockId = blockCounter_++;
    auto* trueBB = builder_.createBlock("cond_true_" + std::to_string(blockId));
    auto* falseBB = builder_.createBlock("cond_false_" + std::to_string(blockId));
    auto* endBB = builder_.createBlock("cond_end_" + std::to_string(blockId));

    builder_.createCondBranch(cond, trueBB, falseBB);

    builder_.setInsertPoint(trueBB);
    currentBlock_ = trueBB;  // Keep ASTToHIR's currentBlock_ in sync
    auto trueVal = lowerExpression(node->whenTrue.get());
    auto boxedTrue = boxValueIfNeeded(trueVal);
    auto* trueEndBB = builder_.getInsertBlock(); // may differ after calls
    builder_.createBranch(endBB);

    builder_.setInsertPoint(falseBB);
    currentBlock_ = falseBB;  // Keep ASTToHIR's currentBlock_ in sync
    auto falseVal = lowerExpression(node->whenFalse.get());
    auto boxedFalse = boxValueIfNeeded(falseVal);
    auto* falseEndBB = builder_.getInsertBlock();
    builder_.createBranch(endBB);

    builder_.setInsertPoint(endBB);
    currentBlock_ = endBB;  // Keep ASTToHIR's currentBlock_ in sync
    lastValue_ = builder_.createPhi(HIRType::makeAny(),
        {{boxedTrue, trueEndBB}, {boxedFalse, falseEndBB}});
}

void ASTToHIR::visitAssignmentExpression(ast::AssignmentExpression* node) {
    setSourceLine(node);
    // Inside a `with` body, an identifier LHS resolves its reference BEFORE
    // the RHS evaluates (ES 13.15.2: lref first, then rval; PutValue uses the
    // INITIAL reference even if RHS side effects later add the name to the
    // with-object — S11.13.1_A6_T3). ts_with_ref snapshots which with-object
    // (if any) holds the binding; the store below honors that snapshot.
    std::shared_ptr<HIRValue> withRef = nullptr;
    if (withScopeActive()) {
        if (auto* lhsIdent = dynamic_cast<ast::Identifier*>(node->left.get())) {
            auto nameC = builder_.createConstString(lhsIdent->name);
            withRef = builder_.createCall("ts_with_ref", {nameC}, HIRType::makeAny());
        }
    }
    auto rhs = lowerExpression(node->right.get());

    // Handle simple identifier assignment
    auto* ident = dynamic_cast<ast::Identifier*>(node->left.get());
    if (ident) {
        // For module-scoped variables accessed from inner functions, use __modvar_ globals
        // instead of closure cells. Closure cells are per-closure snapshots, but module
        // variables must be shared across all functions in the module.
        if (currentFunction_ && isModuleGlobalVar(ident->name)) {
            size_t scopeIndex = 0;
            if (isCapturedVariable(ident->name, &scopeIndex)) {
                // Mark as used-by-inner so reads in __module_init take the
                // global path instead of the stale local fast-path alloca.
                moduleGlobalsUsedByInnerByModule_[ident->name].insert(currentModulePath_);
                builder_.createStoreGlobal(modVarName(ident->name), rhs);
                lastValue_ = rhs;
                return;
            }
        }

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
            // Inside a `with` body a write to a statically-resolved name must
            // still consult the with-scope chain (ES 14.11) — using the
            // reference SNAPSHOT taken before RHS evaluation (withRef).
            if (withScopeActive() && withRef) {
                auto nameC = builder_.createConstString(ident->name);
                auto strictC = builder_.createConstInt(strictCode_ ? 1 : 0);
                auto wrote = builder_.createCall("ts_with_set_ref_s",
                    {withRef, nameC, boxValueIfNeeded(rhs), strictC}, HIRType::makeAny());
                int bid = blockCounter_++;
                auto* storeBB = createBlock("withasn.store" + std::to_string(bid));
                auto* contBB = createBlock("withasn.cont" + std::to_string(bid));
                builder_.createCondBranch(wrote, contBB, storeBB);
                builder_.setInsertPoint(storeBB);
                currentBlock_ = storeBB;
                builder_.createStore(rhs, info->value, info->elemType);
                broadcastCaptureWrite(info, rhs);
                builder_.createBranch(contBB);
                builder_.setInsertPoint(contBB);
                currentBlock_ = contBB;
                lastValue_ = rhs;
                return;
            }
            // Emit store to the alloca, with type info for coercion
            builder_.createStore(rhs, info->value, info->elemType);
            broadcastCaptureWrite(info, rhs);
        } else if (info) {
            // Direct value - promote to alloca for mutability
            // Create new alloca and store
            auto allocaPtr = builder_.createAlloca(rhs->type, ident->name);
            builder_.createStore(rhs, allocaPtr, rhs->type);
            // Update variable info to be alloca-based
            info->value = allocaPtr;
            info->elemType = rhs->type;
            info->isAlloca = true;
        } else if (withScopeActive() && withRef) {
            // Inside a `with` body, an assignment to a name with no static
            // binding writes the SNAPSHOTTED with-object (reference taken
            // before RHS evaluation) or falls back to a sloppy implicit
            // global. ES 14.11 / 13.15.2.
            auto nameStr = builder_.createConstString(ident->name);
            auto strictC = builder_.createConstInt(strictCode_ ? 1 : 0);
            builder_.createCall("ts_with_set_ref_or_global_s",
                {withRef, nameStr, boxValueIfNeeded(rhs), strictC}, HIRType::makeVoid());
        } else if (!ident->isUnresolvedReference) {
            // Untyped-JS implicit global: a bare assignment to a name the analyzer
            // resolved (NOT the TS "Undefined variable"/isUnresolvedReference case)
            // with no prior binding is a sloppy-mode implicit global. Give it real
            // global storage so reads/updates persist across loop iterations —
            // without this `for (i = 0; i <= 1; i++) {}` stored the increment into
            // a dead SSA value and looped forever.
            moduleGlobalVarsByModule_[ident->name].insert(currentModulePath_);
            module_->globals[modVarName(ident->name)] = HIRType::makeAny();
            builder_.createStoreGlobal(modVarName(ident->name), rhs);
        } else {
            // New variable - should not happen in assignment, but handle gracefully
            defineVariable(ident->name, rhs);
        }

        // If this variable is a module-scoped global, also update the __modvar_ global
        // so other functions (arrow functions, function expressions) from the same module
        // can read the updated value via LoadGlobalTyped.
        if (isModuleGlobalVar(ident->name)) {
            builder_.createStoreGlobal(modVarName(ident->name), rhs);
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
                        // Store to the static property global (authoritative for
                        // the literal-name direct-read fast path).
                        auto globalPtr = it->second.first;
                        auto propType = it->second.second;
                        builder_.createStore(rhs, globalPtr, propType);
                        // Mirror onto the constructor closure so a non-literal
                        // reference (alias / dynamic key / passed) reads the
                        // updated value rather than the stale init.
                        auto ctorVal = builder_.createLoadFunction(cls->name + "_constructor");
                        builder_.createSetPropStatic(ctorVal, privateStorageKey(propAccess->name), rhs);
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
            // Skip nullptr placeholders (see getter path comment) — same UAF
            // family for private-setter-before-super class-body lowering.
            HIRFunction* setterFunc = nullptr;
            if (setterIt != targetClass->methods.end() && setterIt->second) {
                setterFunc = setterIt->second;
            } else {
                // Static accessors live in staticMethods (not methods), so a write
                // to a STATIC setter — `this.#x = v` inside a static method, or
                // `C.#x = v` — wasn't dispatched and silently stored a data
                // property, bypassing `static set #x`. Look there too.
                auto sit = targetClass->staticMethods.find(setterKey);
                if (sit != targetClass->staticMethods.end() && sit->second) {
                    setterFunc = sit->second;
                    // The staticMethods entry is the EMPTY module-level stub
                    // (`<Class>___setter_<name>`, body == `ret`); the real
                    // monomorphized body is emitted under a different symbol.
                    // Call that by name (the linker resolves it) so the static
                    // setter actually runs. Mirrors HIRToLLVM_Closures private
                    // accessor mangling (`<Cls>_static_set___private_<Cls>_<m>`).
                    size_t ic = 0; for (auto& b : setterFunc->blocks) ic += b->instructions.size();
                    if (ic <= 1) {
                        std::string member = propAccess->name;
                        std::string real;
                        if (!member.empty() && member[0] == '#')
                            real = targetClass->name + "_static_set___private_" +
                                   targetClass->name + "_" + member.substr(1);
                        else
                            real = targetClass->name + "_static_set_" + member;
                        // The static setter body takes ONLY the value (its `this`
                        // is the class, resolved at compile time) — unlike instance
                        // setters which take (this, value).
                        builder_.createCall(real, {rhs}, HIRType::makeVoid());
                        lastValue_ = rhs;
                        return;
                    }
                }
            }
            if (setterFunc) {
                // Found a setter - call it instead of direct property assignment
                builder_.createCall(setterFunc->name, {obj, rhs}, HIRType::makeVoid());
                lastValue_ = rhs;
                return;
            }
        }

        // Private member WRITE brand check: on an untyped receiver `obj.#x = v`
        // must throw TypeError if obj is not an instance of the declaring class
        // (ts_object_set_private validates the hidden field / private setter is
        // present). A typed receiver is a provable instance → direct hidden-key set.
        if (!propAccess->name.empty() && propAccess->name[0] == '#'
            && obj->type && obj->type->kind == HIRTypeKind::Any) {
            auto keyStr = builder_.createConstString(propAccess->name);
            builder_.createCall("ts_object_set_private",
                {obj, keyStr, boxValueIfNeeded(rhs)}, HIRType::makeVoid());
            lastValue_ = rhs;
            return;
        }
        // Strict code with a DYNAMIC (Any) or plain-Object receiver (object
        // literals are typed Object): route through the strict runtime entry
        // so a blocked write throws TypeError (PutValue throw=true). Typed
        // CLASS receivers keep the SetPropStatic fast path (their shape slots
        // are plain writable data properties).
        if (strictCode_ && obj->type &&
            (obj->type->kind == HIRTypeKind::Any ||
             obj->type->kind == HIRTypeKind::Object) &&
            (propAccess->name.empty() || propAccess->name[0] != '#')) {
            auto keyStr = builder_.createConstString(propAccess->name);
            builder_.createCall("ts_object_set_property_strict",
                {obj, keyStr, boxValueIfNeeded(rhs)}, HIRType::makeVoid());
            lastValue_ = rhs;
            return;
        }
        builder_.createSetPropStatic(obj, privateStorageKey(propAccess->name), rhs);
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

    // Handle destructuring assignment: `[a, b = 1, ...rest] = arr` (LHS is
    // an ArrayLiteralExpression because the parser cannot distinguish the
    // assignment-target from the value form until it sees the `=`). Each
    // element is one of:
    //   - Identifier: simple assignment of source[i]
    //   - AssignmentExpression (target = default): use default when
    //     source[i] is undefined
    //   - SpreadElement (...rest): assign source.slice(i) to rest
    //   - OmittedExpression: skip the slot
    // Without this branch, `[x, y] = [1, 2]` falls through and stores
    // nothing — variables remain undefined.
    // Destructuring assignment: `[a,b] = arr` / `({a, b: t} = src)`. The parser
    // can't distinguish an assignment-target pattern from a value array/object
    // literal until it sees `=`, so the LHS arrives as an Array/Object literal.
    // Delegated to the recursive destructureAssignmentPattern (handles nesting,
    // defaults, computed keys, and rest).
    if (dynamic_cast<ast::ArrayLiteralExpression*>(node->left.get()) ||
        dynamic_cast<ast::ObjectLiteralExpression*>(node->left.get())) {
        destructureAssignmentPattern(node->left.get(), rhs);
        lastValue_ = rhs;
        return;
    }

    lastValue_ = rhs;
}

void ASTToHIR::assignDestructureName(const std::string& name,
                                     std::shared_ptr<HIRValue> value) {
    if (currentFunction_ && isModuleGlobalVar(name)) {
        size_t scopeIndex = 0;
        if (isCapturedVariable(name, &scopeIndex)) {
            moduleGlobalsUsedByInnerByModule_[name].insert(currentModulePath_);
            builder_.createStoreGlobal(modVarName(name), value);
            return;
        }
    }
    size_t scopeIndex = 0;
    if (currentFunction_ && isCapturedVariable(name, &scopeIndex)) {
        auto* info = lookupVariableInfo(name);
        auto type = info && info->elemType ? info->elemType : value->type;
        registerCapture(name, type, scopeIndex);
        currentFunction_->hasClosure = true;
        builder_.createStoreCapture(name, value);
        return;
    }
    auto* info = lookupVariableInfo(name);
    if (info && info->isAlloca) {
        builder_.createStore(value, info->value, info->elemType);
        broadcastCaptureWrite(info, value);
    } else if (info) {
        auto allocaPtr = builder_.createAlloca(value->type, name);
        builder_.createStore(value, allocaPtr, value->type);
        info->value = allocaPtr;
        info->elemType = value->type;
        info->isAlloca = true;
    } else {
        defineVariable(name, value);
    }
    if (isModuleGlobalVar(name)) {
        builder_.createStoreGlobal(modVarName(name), value);
    }
}

void ASTToHIR::destructureAssignmentPattern(ast::Expression* lhs,
                                            std::shared_ptr<HIRValue> rhs) {
    // Array assignment pattern: `[a, b = 1, ...rest] = src`.
    if (auto* arrLit = dynamic_cast<ast::ArrayLiteralExpression*>(lhs)) {
        // ECMA-262 13.15.5.1 early errors: rest must be LAST and have no default.
        for (size_t i = 0; i < arrLit->elements.size(); ++i) {
            auto* slot = arrLit->elements[i].get();
            if (auto* sp = dynamic_cast<ast::SpreadElement*>(slot)) {
                if (i + 1 != arrLit->elements.size()) {
                    throw std::runtime_error("SyntaxError: Rest element must be last element in destructuring pattern");
                }
                if (dynamic_cast<ast::AssignmentExpression*>(sp->expression.get())) {
                    throw std::runtime_error("SyntaxError: Rest element cannot have a default initializer");
                }
            }
        }
        builder_.createCall("ts_destructure_require_object", {rhs},
                            HIRType::makeVoid());
        int64_t consumeCount = 0;
        bool hasRest = false;
        for (auto& e : arrLit->elements) {
            if (dynamic_cast<ast::SpreadElement*>(e.get())) { hasRest = true; break; }
            consumeCount++;
        }
        auto source = builder_.createCall(
            "ts_destructure_iterate",
            { rhs, builder_.createConstInt(consumeCount),
              builder_.createConstInt(hasRest ? 1 : 0) },
            HIRType::makeArray(HIRType::makeAny()));
        auto assignToTarget = [&](ast::Expression* target,
                                  std::shared_ptr<HIRValue> value) {
            if (auto* tgt = dynamic_cast<ast::Identifier*>(target)) {
                assignDestructureName(tgt->name, value);
                return;
            }
            if (auto* tgt = dynamic_cast<ast::PropertyAccessExpression*>(target)) {
                auto obj = lowerExpression(tgt->expression.get());
                builder_.createSetPropStatic(obj, privateStorageKey(tgt->name), value);
                return;
            }
            if (auto* tgt = dynamic_cast<ast::ElementAccessExpression*>(target)) {
                auto obj = lowerExpression(tgt->expression.get());
                auto idx = lowerExpression(tgt->argumentExpression.get());
                builder_.createSetElem(obj, idx, value);
                return;
            }
            // Nested pattern: `[ {a}, [b] ] = src` — recurse.
            if (dynamic_cast<ast::ArrayLiteralExpression*>(target) ||
                dynamic_cast<ast::ObjectLiteralExpression*>(target)) {
                destructureAssignmentPattern(target, value);
                return;
            }
        };
        int64_t index = 0;
        for (auto& elemPtr : arrLit->elements) {
            ast::Expression* elem = elemPtr.get();
            if (!elem || dynamic_cast<ast::OmittedExpression*>(elem)) { ++index; continue; }
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(elem)) {
                auto idxConst = builder_.createConstInt(index);
                auto restVal = builder_.createCallMethod(source, "slice",
                    {idxConst}, HIRType::makeAny());
                if (auto* tgtExpr = dynamic_cast<ast::Expression*>(spread->expression.get())) {
                    assignToTarget(tgtExpr, restVal);
                }
                ++index;
                continue;
            }
            auto idxConst = builder_.createConstInt(index);
            auto extracted = builder_.createGetElem(source, idxConst, HIRType::makeAny());
            ast::Expression* target = elem;
            if (auto* assignDefault = dynamic_cast<ast::AssignmentExpression*>(elem)) {
                auto* defaultExpr = dynamic_cast<ast::Expression*>(assignDefault->right.get());
                if (defaultExpr) {
                    auto isUndef = builder_.createIsUndefined(extracted);
                    // ECMA-262: the Initializer is evaluated only in the "value
                    // is undefined" step, so a SKIPPED side-effecting default
                    // must NOT run (`[a = se()] = [1]` -> se not called). The old
                    // createSelect evaluated both operands eagerly. Mirror the
                    // binding path (lowerBindingElement): branch + merge slot.
                    // NamedEvaluation (anon fn/arrow/class default takes the
                    // target name) stays inside the default block so it can't
                    // leak when skipped.
                    auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "dstr_dflt");
                    auto* defaultBB = currentFunction_->createBlock("dstr_default");
                    auto* usedBB = currentFunction_->createBlock("dstr_used");
                    auto* mergeBB = currentFunction_->createBlock("dstr_merge");
                    builder_.createCondBranch(isUndef, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
                    std::string savedPCDN = pendingClosureDisplayName_;
                    if (auto* tid = dynamic_cast<ast::Identifier*>(assignDefault->left.get())) {
                        if (dynamic_cast<ast::ArrowFunction*>(defaultExpr) ||
                            dynamic_cast<ast::FunctionExpression*>(defaultExpr) ||
                            dynamic_cast<ast::ClassExpression*>(defaultExpr)) {
                            pendingClosureDisplayName_ = tid->name;
                        }
                    }
                    auto defaultValue = boxValueIfNeeded(lowerExpression(defaultExpr));
                    pendingClosureDisplayName_ = savedPCDN;
                    builder_.createStore(defaultValue, mergeSlot);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB); currentBlock_ = usedBB;
                    builder_.createStore(boxValueIfNeeded(extracted), mergeSlot);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
                    extracted = builder_.createLoad(HIRType::makeAny(), mergeSlot);
                }
                target = dynamic_cast<ast::Expression*>(assignDefault->left.get());
            }
            if (target) assignToTarget(target, extracted);
            ++index;
        }
        return;
    }

    // Object assignment pattern: `({a, b: t, c = 1, ...rest} = src)`.
    if (auto* objLit = dynamic_cast<ast::ObjectLiteralExpression*>(lhs)) {
        builder_.createCall("ts_destructure_require_object", {rhs},
                            HIRType::makeVoid());
        auto assignToTarget = [&](ast::Expression* target,
                                  std::shared_ptr<HIRValue> value) {
            if (auto* tgt = dynamic_cast<ast::Identifier*>(target)) {
                assignDestructureName(tgt->name, value);
                return;
            }
            if (auto* tgt = dynamic_cast<ast::PropertyAccessExpression*>(target)) {
                auto obj = lowerExpression(tgt->expression.get());
                builder_.createSetPropStatic(obj, privateStorageKey(tgt->name), value);
                return;
            }
            if (auto* tgt = dynamic_cast<ast::ElementAccessExpression*>(target)) {
                auto obj = lowerExpression(tgt->expression.get());
                auto idx = lowerExpression(tgt->argumentExpression.get());
                builder_.createSetElem(obj, idx, value);
                return;
            }
            // Nested pattern: `({a: {b}, c: [d]} = src)` — recurse.
            if (dynamic_cast<ast::ArrayLiteralExpression*>(target) ||
                dynamic_cast<ast::ObjectLiteralExpression*>(target)) {
                destructureAssignmentPattern(target, value);
                return;
            }
        };
        std::vector<std::shared_ptr<HIRValue>> consumedKeys;
        for (auto& propPtr : objLit->properties) {
            ast::Node* prop = propPtr.get();
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(prop)) {
                auto keysArr = builder_.createCall(
                    "ts_array_create", {},
                    HIRType::makeArray(HIRType::makeAny(), false));
                for (auto& k : consumedKeys) {
                    builder_.createCall("ts_array_push", {keysArr, k},
                                        HIRType::makeVoid());
                }
                auto restObj = builder_.createCall(
                    "ts_object_rest_exclude", {rhs, keysArr}, HIRType::makeAny());
                if (auto* tgt = dynamic_cast<ast::Expression*>(spread->expression.get())) {
                    assignToTarget(tgt, restObj);
                }
                continue;
            }
            ast::Expression* target = nullptr;
            ast::Expression* defaultExpr = nullptr;
            std::shared_ptr<HIRValue> extracted;
            std::shared_ptr<HIRValue> keyForExclude;
            if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(prop)) {
                if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(pa->nameNode.get())) {
                    keyForExclude = boxValueIfNeeded(lowerExpression(cpn->expression.get()));
                    extracted = builder_.createGetPropDynamic(rhs, keyForExclude);
                } else {
                    keyForExclude = builder_.createConstString(pa->name);
                    extracted = builder_.createGetPropDynamic(rhs, keyForExclude);
                }
                ast::Expression* init = dynamic_cast<ast::Expression*>(pa->initializer.get());
                if (auto* assignDefault = dynamic_cast<ast::AssignmentExpression*>(init)) {
                    defaultExpr = dynamic_cast<ast::Expression*>(assignDefault->right.get());
                    target = dynamic_cast<ast::Expression*>(assignDefault->left.get());
                } else {
                    target = init;
                }
            } else if (auto* sh = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop)) {
                keyForExclude = builder_.createConstString(sh->name);
                extracted = builder_.createGetPropDynamic(rhs, keyForExclude);
                if (auto* dflt = dynamic_cast<ast::Expression*>(sh->initializer.get())) {
                    // Lazy default: a skipped side-effecting default must not run.
                    auto isUndef = builder_.createIsUndefined(extracted);
                    auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "dstr_dflt");
                    auto* defaultBB = currentFunction_->createBlock("dstr_default");
                    auto* usedBB = currentFunction_->createBlock("dstr_used");
                    auto* mergeBB = currentFunction_->createBlock("dstr_merge");
                    builder_.createCondBranch(isUndef, defaultBB, usedBB);
                    builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
                    std::string savedPCDN = pendingClosureDisplayName_;
                    if (dynamic_cast<ast::ArrowFunction*>(dflt) ||
                        dynamic_cast<ast::FunctionExpression*>(dflt) ||
                        dynamic_cast<ast::ClassExpression*>(dflt)) {
                        pendingClosureDisplayName_ = sh->name;
                    }
                    auto defaultValue = boxValueIfNeeded(lowerExpression(dflt));
                    pendingClosureDisplayName_ = savedPCDN;
                    builder_.createStore(defaultValue, mergeSlot);
                    builder_.createBranch(mergeBB);
                    builder_.setInsertPoint(usedBB); currentBlock_ = usedBB;
                    builder_.createStore(boxValueIfNeeded(extracted), mergeSlot);
                    builder_.createBranch(mergeBB);
                    builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
                    extracted = builder_.createLoad(HIRType::makeAny(), mergeSlot);
                }
                assignDestructureName(sh->name, extracted);
                consumedKeys.push_back(keyForExclude);
                continue;
            } else {
                continue;
            }
            if (keyForExclude) consumedKeys.push_back(keyForExclude);
            if (defaultExpr) {
                // Lazy default: a skipped side-effecting default must not run.
                auto isUndef = builder_.createIsUndefined(extracted);
                auto mergeSlot = builder_.createAlloca(HIRType::makeAny(), "dstr_dflt");
                auto* defaultBB = currentFunction_->createBlock("dstr_default");
                auto* usedBB = currentFunction_->createBlock("dstr_used");
                auto* mergeBB = currentFunction_->createBlock("dstr_merge");
                builder_.createCondBranch(isUndef, defaultBB, usedBB);
                builder_.setInsertPoint(defaultBB); currentBlock_ = defaultBB;
                std::string savedPCDN = pendingClosureDisplayName_;
                if (auto* tid = dynamic_cast<ast::Identifier*>(target)) {
                    if (dynamic_cast<ast::ArrowFunction*>(defaultExpr) ||
                        dynamic_cast<ast::FunctionExpression*>(defaultExpr) ||
                        dynamic_cast<ast::ClassExpression*>(defaultExpr)) {
                        pendingClosureDisplayName_ = tid->name;
                    }
                }
                auto defaultValue = boxValueIfNeeded(lowerExpression(defaultExpr));
                pendingClosureDisplayName_ = savedPCDN;
                builder_.createStore(defaultValue, mergeSlot);
                builder_.createBranch(mergeBB);
                builder_.setInsertPoint(usedBB); currentBlock_ = usedBB;
                builder_.createStore(boxValueIfNeeded(extracted), mergeSlot);
                builder_.createBranch(mergeBB);
                builder_.setInsertPoint(mergeBB); currentBlock_ = mergeBB;
                extracted = builder_.createLoad(HIRType::makeAny(), mergeSlot);
            }
            if (target) assignToTarget(target, extracted);
        }
        return;
    }
}


}  // namespace ts::hir
