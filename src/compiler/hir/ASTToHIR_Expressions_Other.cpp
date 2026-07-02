#include "ASTToHIR_Internal.h"

namespace ts::hir {


void ASTToHIR::visitParenthesizedExpression(ast::ParenthesizedExpression* node) {
    setSourceLine(node);
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitArrayLiteralExpression(ast::ArrayLiteralExpression* node) {
    setSourceLine(node);
    // Try to infer element type from the array's inferred type
    std::shared_ptr<HIRType> elemType = HIRType::makeAny();
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Array) {
        auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->inferredType);
        if (arrayType->elementType) {
            elemType = convertType(arrayType->elementType);
        }
    }

    // Check if we have any spread elements - if so, we need dynamic approach
    bool hasSpread = false;
    for (auto& elem : node->elements) {
        if (dynamic_cast<ast::SpreadElement*>(elem.get())) {
            hasSpread = true;
            break;
        }
    }

    if (hasSpread) {
        // With spread elements, use ts_array_create and dynamic push/concat.
        // Inside a generator/async function, sub-expressions can yield —
        // splitting the body across resume-blocks where the SSA value of
        // `arr` no longer dominates the next concat/push site. Spill the
        // accumulator to an alloca so reloads work across resume boundaries.
        bool inGenerator = currentFunction_ && (currentFunction_->isGenerator || currentFunction_->isAsync);
        auto arrType = HIRType::makeArray(elemType, false);
        auto initial = builder_.createCall("ts_array_create", {}, arrType);
        std::shared_ptr<HIRValue> arrSlot;
        std::shared_ptr<HIRValue> arr = initial;
        if (inGenerator) {
            arrSlot = builder_.createAlloca(arrType, "arrlit.acc");
            builder_.createStore(initial, arrSlot);
        }
        auto reload = [&]() {
            return inGenerator ? builder_.createLoad(arrType, arrSlot) : arr;
        };
        auto store = [&](std::shared_ptr<HIRValue> v) {
            if (inGenerator) builder_.createStore(v, arrSlot);
            arr = v;
        };

        for (auto& elem : node->elements) {
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(elem.get())) {
                // Spread element in array literal: per ECMA-262 13.2.4.1
                // SpreadElement evaluation uses the iterator protocol
                // (@@iterator + next()), NOT Array.prototype.concat's
                // IsConcatSpreadable. ts_array_spread_into handles both
                // TsArray fast-path and generic iterables (generators, etc.).
                auto spreadArr = lowerExpression(spread->expression.get());
                auto concat = builder_.createCall("ts_array_spread_into", {reload(), spreadArr}, arrType);
                store(concat);
            } else {
                // Regular element: push it.
                auto elemVal = lowerExpression(elem.get());
                builder_.createCall("ts_array_push", {reload(), elemVal}, HIRType::makeInt64());
            }
        }

        lastValue_ = reload();
    } else {
        // No spread elements - use efficient pre-allocated array.
        // createNewArrayBoxed lowers to ts_array_create_sized which fills
        // slots with NANBOX_HOLE. Regular elements overwrite those slots;
        // elided positions (OmittedExpression, i.e. `[, 1, 2]`) stay as
        // holes per ECMA-262 §13.2.4 ArrayLiteral Elision semantics.
        auto lenVal = builder_.createConstInt(static_cast<int64_t>(node->elements.size()));
        auto arr = builder_.createNewArrayBoxed(lenVal, elemType);

        int64_t idx = 0;
        for (auto& elem : node->elements) {
            if (dynamic_cast<ast::OmittedExpression*>(elem.get())) {
                idx++;  // leave NANBOX_HOLE sentinel in place
                continue;
            }
            auto elemVal = lowerExpression(elem.get());
            auto idxVal = builder_.createConstInt(idx++);
            builder_.createSetElem(arr, idxVal, elemVal);
        }

        lastValue_ = arr;
    }
}

void ASTToHIR::visitElementAccessExpression(ast::ElementAccessExpression* node) {
    setSourceLine(node);
    // `super[key]` READ with a literal key: dispatch base-class getter/method.
    if (dynamic_cast<ast::SuperExpression*>(node->expression.get()) &&
        currentClass_ && currentClass_->baseClass) {
        std::string key;
        if (auto* sl = dynamic_cast<ast::StringLiteral*>(node->argumentExpression.get()))
            key = sl->value;
        else if (auto* nl = dynamic_cast<ast::NumericLiteral*>(node->argumentExpression.get()))
            key = std::to_string((int64_t)nl->value);
        if (!key.empty()) {
            bool inStatic = currentFunction_ &&
                currentFunction_->name.find("_static_") != std::string::npos;
            auto thisVal = lookupVariable("this");
            if (!thisVal) thisVal = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
            for (HIRClass* sc = currentClass_->baseClass; sc; sc = sc->baseClass) {
                auto& tbl = inStatic ? sc->staticMethods : sc->methods;
                auto git = tbl.find("__getter_" + key);
                if (git != tbl.end() && git->second) {
                    std::string real = completeMethodSymbol(sc, "__getter_" + key, git->second, inStatic);
                    lastValue_ = inStatic ? builder_.createCall(real, {}, HIRType::makeAny())
                                          : builder_.createCall(real, {thisVal}, HIRType::makeAny());
                    return;
                }
                auto mit = tbl.find(key);
                if (mit != tbl.end() && mit->second) {
                    lastValue_ = builder_.createLoadFunction(mit->second->name);
                    return;
                }
            }
        }
    }
    // Check for enum reverse mapping: EnumName[numericValue]
    auto* classNameIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (classNameIdent) {
        auto enumReverseIt = enumReverseMap_.find(classNameIdent->name);
        if (enumReverseIt != enumReverseMap_.end()) {
            // This is an enum reverse mapping access
            if (auto* numLit = dynamic_cast<ast::NumericLiteral*>(node->argumentExpression.get())) {
                // Constant index - look up at compile time
                int64_t idx = static_cast<int64_t>(numLit->value);
                auto memberIt = enumReverseIt->second.find(idx);
                if (memberIt != enumReverseIt->second.end()) {
                    lastValue_ = builder_.createConstString(memberIt->second);
                    return;
                }
            }
            // Dynamic index - need runtime lookup (TODO: generate runtime object for dynamic access)
            // For now, fall through to dynamic access
        }
    }

    auto obj = lowerExpression(node->expression.get());

    // Handle optional chaining: obj?.[idx]
    if (node->isOptional) {
        // Check if obj is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {obj}, HIRType::makeBool());

        // Create undefined value before branching (so it's in the current block)
        auto undef = builder_.createConstUndefined();

        // Create blocks for conditional access (with unique names)
        int blockId = blockCounter_++;
        auto* accessBlock = builder_.createBlock("opt_access_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("opt_merge_" + std::to_string(blockId));

        // Branch based on nullish check
        auto* currentBlock = builder_.getInsertBlock();
        builder_.createCondBranch(isNullish, mergeBlock, accessBlock);

        // Access block: perform the element access
        builder_.setInsertPoint(accessBlock);
        auto idx = lowerExpression(node->argumentExpression.get());
        auto accessResult = builder_.createGetElem(obj, idx);
        auto* finalAccessBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge block: phi node to select result
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(undef, currentBlock));
        phiIncoming.push_back(std::make_pair(accessResult, finalAccessBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    auto idx = lowerExpression(node->argumentExpression.get());
    lastValue_ = builder_.createGetElem(obj, idx);
}

void ASTToHIR::visitPropertyAccessExpression(ast::PropertyAccessExpression* node) {
    setSourceLine(node);
    // `new.target` meta-property (ES 13.3.12): the parser encodes it as a
    // PropertyAccess with base Identifier "new" (a keyword, so no user
    // variable can collide). Reads the ambient construct-target register —
    // set by the construct paths, undefined during a plain [[Call]].
    if (node->name == "target") {
        if (auto* baseId = dynamic_cast<ast::Identifier*>(node->expression.get());
            baseId && baseId->name == "new") {
            lastValue_ = builder_.createCall("ts_get_new_target", {}, HIRType::makeAny());
            return;
        }
    }
    // `super.prop` READ (not a call): dispatch a base-class getter with `this`,
    // or load a base-class method. (super.method() calls are handled in the call
    // path; this covers `super.getter` and `super.method` without invocation.)
    if (dynamic_cast<ast::SuperExpression*>(node->expression.get()) &&
        currentClass_ && currentClass_->baseClass) {
        bool inStatic = currentFunction_ &&
            currentFunction_->name.find("_static_") != std::string::npos;
        auto thisVal = lookupVariable("this");
        if (!thisVal) thisVal = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
        for (HIRClass* sc = currentClass_->baseClass; sc; sc = sc->baseClass) {
            if (inStatic) {
                // Static `super.g` / `super.m`: base-class STATIC accessor/method
                // (no `this`).
                auto sg = sc->staticMethods.find("__getter_" + node->name);
                if (sg != sc->staticMethods.end() && sg->second) {
                    std::string real = completeMethodSymbol(sc, "__getter_" + node->name, sg->second, true);
                    lastValue_ = builder_.createCall(real, {}, HIRType::makeAny());
                    return;
                }
                auto sm = sc->staticMethods.find(node->name);
                if (sm != sc->staticMethods.end() && sm->second) {
                    lastValue_ = builder_.createLoadFunction(sm->second->name);
                    return;
                }
                continue;
            }
            auto git = sc->methods.find("__getter_" + node->name);
            if (git != sc->methods.end() && git->second) {
                std::string real = completeMethodSymbol(sc, "__getter_" + node->name, git->second, false);
                lastValue_ = builder_.createCall(real, {thisVal}, HIRType::makeAny());
                return;
            }
            auto mit = sc->methods.find(node->name);
            if (mit != sc->methods.end() && mit->second) {
                lastValue_ = builder_.createLoadFunction(mit->second->name);
                return;
            }
        }
    }
    // Check for static property access: ClassName.propertyName
    auto* classNameIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
    if (classNameIdent) {
        // Check for enum member access: EnumName.MemberName
        auto enumIt = enumValues_.find(classNameIdent->name);
        if (enumIt != enumValues_.end()) {
            auto memberIt = enumIt->second.find(node->name);
            if (memberIt != enumIt->second.end()) {
                const EnumValue& ev = memberIt->second;
                if (ev.isString) {
                    lastValue_ = builder_.createConstString(ev.strValue);
                } else {
                    // Use float64 for consistency with JS number semantics
                    lastValue_ = builder_.createConstFloat(static_cast<double>(ev.numValue));
                }
                return;
            }
        }

        for (auto& cls : module_->classes) {
            if (cls->name == classNameIdent->name) {
                // Check if this is a static property
                std::string globalName = cls->name + "_static_" + node->name;
                auto it = staticPropertyGlobals_.find(globalName);
                if (it != staticPropertyGlobals_.end()) {
                    // Load from the static property global
                    auto globalPtr = it->second.first;
                    auto propType = it->second.second;
                    lastValue_ = builder_.createLoad(propType, globalPtr);
                    return;
                }
                break;
            }
        }

        // Check for namespace property access: ns.prop where ns is a namespace import
        // Only intercept for user-defined modules; extension modules fall through
        // to normal dispatch via lowerExpression + extension registry.
        if (classNameIdent->inferredType &&
            classNameIdent->inferredType->kind == ts::TypeKind::Namespace) {

            // Check specializations first (always complete, not affected by processing order)
            if (specializations_) {
                for (const auto& spec : *specializations_) {
                    if (spec.originalName == node->name || spec.specializedName == node->name) {
                        auto funcType = HIRType::makeFunction();
                        lastValue_ = builder_.createLoadFunction(spec.specializedName, funcType);
                        return;
                    }
                }
            }

            // Check already-processed HIR functions
            for (const auto& func : module_->functions) {
                if (func->name == node->name) {
                    auto funcType = HIRType::makeFunction();
                    funcType->returnType = func->returnType;
                    for (const auto& param : func->params) {
                        funcType->paramTypes.push_back(param.second);
                    }
                    lastValue_ = builder_.createLoadFunction(node->name, funcType);
                    return;
                }
            }

            // Check for module-level globals (exported variables)
            std::string globalName = modVarName(node->name);
            auto globalVar = lookupVariable(globalName);
            if (globalVar) {
                lastValue_ = globalVar;
                return;
            }

            // Check for enum member access through namespace
            for (const auto& enumPair : enumValues_) {
                auto memberIt = enumPair.second.find(node->name);
                if (memberIt != enumPair.second.end()) {
                    const EnumValue& ev = memberIt->second;
                    if (ev.isString) {
                        lastValue_ = builder_.createConstString(ev.strValue);
                    } else {
                        lastValue_ = builder_.createConstFloat(static_cast<double>(ev.numValue));
                    }
                    return;
                }
            }

            // If nothing found, fall through to normal dispatch
            // (extension modules are handled via lowerExpression + extension registry)
        }
    }

    auto obj = lowerExpression(node->expression.get());

    // Determine the property type - check if this is 'this' access in a class context
    std::shared_ptr<HIRType> propType = HIRType::makeAny();

    // Special handling for built-in type properties
    if (node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;

        // Array.length returns a number - call ts_array_length directly
        if (exprType->kind == ts::TypeKind::Array && node->name == "length") {
            lastValue_ = builder_.createCall("ts_array_length", {obj}, HIRType::makeInt64());
            return;
        }
        // String.length returns a number - call ts_string_length directly
        else if (exprType->kind == ts::TypeKind::String && node->name == "length") {
            lastValue_ = builder_.createCall("ts_string_length", {obj}, HIRType::makeInt64());
            return;
        }
    }

    if (currentClass_) {
        // Check if the expression is 'this'
        auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_->shape) {
            // Look up the property type from the class shape
            auto typeIt = currentClass_->shape->propertyTypes.find(node->name);
            if (typeIt != currentClass_->shape->propertyTypes.end()) {
                propType = typeIt->second;
            }
        }
    }

    // Strategy B Phase 4a: extend the shape lookup to non-`this` typed
    // receivers. If the receiver expression has a known class type, find
    // the matching HIRClass and look up the property type from its shape.
    // Mirrors the getter-resolution loop just below at lines ~6277-6291.
    //
    // Without this, GetPropStatic emits with propType=Any, the LLVM unbox
    // doesn't fire, and downstream typed operations on property-access
    // results lose precision (Phase 0b probe regression). This is the
    // single change that unblocks Phase 0b, 0c, and 3c.
    if (propType->kind == HIRTypeKind::Any &&
        node->expression && node->expression->inferredType &&
        node->expression->inferredType->kind == ts::TypeKind::Class) {
        auto classType = std::dynamic_pointer_cast<ts::ClassType>(node->expression->inferredType);
        if (classType) {
            for (auto& cls : module_->classes) {
                if (cls->name == classType->name && cls->shape) {
                    auto typeIt = cls->shape->propertyTypes.find(node->name);
                    if (typeIt != cls->shape->propertyTypes.end() && typeIt->second) {
                        propType = typeIt->second;
                    }
                    break;
                }
            }
        }
    }

    // Check for getter: look up the class type and see if it has __getter_<propName>
    HIRClass* targetClass = nullptr;

    // First, check if expression has an inferred class type
    if (node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;
        if (exprType->kind == ts::TypeKind::Class) {
            auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
            if (classType) {
                // Find the HIRClass by name
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
        auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (thisIdent && thisIdent->name == "this" && currentClass_) {
            targetClass = currentClass_;
        }
    }

    // Check if the target class has a getter for this property
    if (targetClass) {
        std::string getterKey = "__getter_" + node->name;
        auto getterIt = targetClass->methods.find(getterKey);
        // Skip nullptr placeholders inserted by the JS pre-scan at line ~771;
        // those get a real HIRFunction* later when the body is lowered.
        // Reading getterFunc->returnType on a nullptr crashes during
        // class-body expression processing (e.g. private-getter access in
        // a derived constructor before super() — visitClassDeclaration
        // lowers inner expressions before method bodies finish registering).
        if (getterIt != targetClass->methods.end() && getterIt->second) {
            // Found a getter - call it instead of direct property access
            HIRFunction* getterFunc = getterIt->second;
            auto returnType = getterFunc->returnType ? getterFunc->returnType : HIRType::makeAny();
            lastValue_ = builder_.createCall(getterFunc->name, {obj}, returnType);
            return;
        }
    }

    // Check ExtensionRegistry for property getters on extension-defined classes
    // (e.g., http2Session.destroyed, http2Stream.pending, buf.length)
    // Only match properties that have both a getter AND a lowering spec (actual runtime function).
    if (!targetClass && node->expression && node->expression->inferredType) {
        auto exprType = node->expression->inferredType;
        if (exprType->kind == ts::TypeKind::Class) {
            auto classType = std::dynamic_pointer_cast<ts::ClassType>(exprType);
            if (classType) {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::PropertyDefinition* propDef = extReg.findProperty(classType->name, node->name);
                if (propDef && propDef->getter && propDef->lowering) {
                    // Property has a getter function with lowering spec - emit a call to it
                    std::string getterFunc = *propDef->getter;
                    auto retType = extTypeRefToHIR(propDef->type);
                    lastValue_ = builder_.createCall(getterFunc, {obj}, retType);
                    return;
                }
            }
        }
    }

    // Check ExtensionRegistry for property getters on module-level objects
    // (e.g., http.STATUS_CODES, http.METHODS)
    if (node->expression) {
        auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
        if (ident) {
            auto& extReg = ext::ExtensionRegistry::instance();
            const ext::PropertyDefinition* propDef = extReg.findObjectProperty(ident->name, node->name);
            if (propDef && propDef->getter && propDef->lowering) {
                std::string getterFunc = *propDef->getter;
                // Map the lowering return type to the correct HIR type
                std::shared_ptr<HIRType> retType;
                switch (propDef->lowering->returns) {
                    case ext::LoweringType::I32:
                    case ext::LoweringType::I1:
                        retType = HIRType::makeBool();
                        break;
                    case ext::LoweringType::I64:
                        retType = HIRType::makeInt64();
                        break;
                    case ext::LoweringType::F64:
                        retType = HIRType::makeFloat64();
                        break;
                    case ext::LoweringType::Void:
                        retType = HIRType::makeVoid();
                        break;
                    default:
                        retType = HIRType::makeAny();
                        break;
                }
                lastValue_ = builder_.createCall(getterFunc, {}, retType);
                return;
            }
        }

        // Check for nested object property getters (e.g., path.posix.sep, path.win32.delimiter)
        auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get());
        if (propAccess) {
            auto* parentIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
            if (parentIdent) {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::PropertyDefinition* propDef = extReg.findNestedObjectProperty(
                    parentIdent->name, propAccess->name, node->name);
                if (propDef && propDef->getter && propDef->lowering) {
                    std::string getterFunc = *propDef->getter;
                    std::shared_ptr<HIRType> retType;
                    switch (propDef->lowering->returns) {
                        case ext::LoweringType::I32:
                        case ext::LoweringType::I1:
                            retType = HIRType::makeBool();
                            break;
                        case ext::LoweringType::I64:
                            retType = HIRType::makeInt64();
                            break;
                        case ext::LoweringType::F64:
                            retType = HIRType::makeFloat64();
                            break;
                        case ext::LoweringType::Void:
                            retType = HIRType::makeVoid();
                            break;
                        default:
                            retType = HIRType::makeAny();
                            break;
                    }
                    lastValue_ = builder_.createCall(getterFunc, {}, retType);
                    return;
                }
            }
        }
    }

    // Handle optional chaining: obj?.prop
    if (node->isOptional) {
        // Check if obj is nullish
        auto isNullish = builder_.createCall("ts_value_is_nullish", {obj}, HIRType::makeBool());

        // Create undefined value before branching (so it's in the current block)
        auto undef = builder_.createConstUndefined();

        // Create blocks for conditional access (with unique names)
        int blockId = blockCounter_++;
        auto* accessBlock = builder_.createBlock("opt_access_" + std::to_string(blockId));
        auto* mergeBlock = builder_.createBlock("opt_merge_" + std::to_string(blockId));

        // Branch based on nullish check
        auto* currentBlock = builder_.getInsertBlock();
        builder_.createCondBranch(isNullish, mergeBlock, accessBlock);

        // Access block: perform the property access
        builder_.setInsertPoint(accessBlock);
        auto accessResult = builder_.createGetPropStatic(obj, node->name, propType);
        auto* finalAccessBlock = builder_.getInsertBlock();
        builder_.createBranch(mergeBlock);

        // Merge block: phi node to select result
        builder_.setInsertPoint(mergeBlock);
        std::vector<std::pair<std::shared_ptr<HIRValue>, HIRBlock*>> phiIncoming;
        phiIncoming.push_back(std::make_pair(undef, currentBlock));
        phiIncoming.push_back(std::make_pair(accessResult, finalAccessBlock));
        lastValue_ = builder_.createPhi(HIRType::makeAny(), phiIncoming);
        return;
    }

    // Phase 9c-iv-B: refine propType from `node->inferredType` for property
    // access where the analyzer knows a more precise type than codegen has
    // locally derived. This is needed for:
    //   - `stream.Readable` (namespace.Class lookup) where the analyzer knows
    //     the result is the Readable class but the propType derivation only
    //     walks module_->classes (which doesn't contain extension classes).
    //   - `extInstance.field` where the receiver is an extension class.
    //
    // Excluded:
    //   - `this.X` accesses (currentClass_ is set): the class may not yet be
    //     in module_->classes during method body lowering, and the existing
    //     shape-lookup path handles `this.X` correctly.
    //   - User-defined class instance accesses (receiver is a Class in
    //     module_->classes): SROA + the existing shape-lookup path handle
    //     these. Refining here would trip SROA expecting typed inline reads
    //     of slots the constructor lowering hasn't yet filled.
    //   - Object/structural-literal receivers: monomorphizer may reuse a
    //     typed specialization for an Any-typed call site, and the structural
    //     type may not match the actual runtime object's shape.
    bool isThisAccess = false;
    if (auto* thisIdent = dynamic_cast<ast::Identifier*>(node->expression.get())) {
        if (thisIdent->name == "this") isThisAccess = true;
    }
    bool receiverIsUserDefinedClass = false;
    if (node->expression && node->expression->inferredType &&
        node->expression->inferredType->kind == ts::TypeKind::Class) {
        auto receiverClass = std::dynamic_pointer_cast<ts::ClassType>(node->expression->inferredType);
        if (receiverClass) {
            for (auto& cls : module_->classes) {
                if (cls->name == receiverClass->name) {
                    receiverIsUserDefinedClass = true;
                    break;
                }
            }
        }
    }
    bool receiverIsObjectLiteral = false;
    if (node->expression && node->expression->inferredType &&
        node->expression->inferredType->kind == ts::TypeKind::Object) {
        receiverIsObjectLiteral = true;
    }
    if (propType->kind == HIRTypeKind::Any &&
        !isThisAccess &&
        !currentClass_ &&
        !receiverIsUserDefinedClass &&
        node->inferredType) {
        auto refined = convertType(node->inferredType);
        if (refined && refined->kind != HIRTypeKind::Any) {
            // For object-literal receivers, only refine to Class/Array/etc.
            // (extension types). NEVER refine to primitive types like f64,
            // because the monomorphizer may reuse a typed specialization for
            // an Any-typed call site, and the structural type may not match
            // the actual runtime object's shape — leading to typed loads of
            // fields that don't exist (NaN out, see Phase 9c-iv-A history).
            bool refinementIsPrimitive =
                refined->kind == HIRTypeKind::Int64 ||
                refined->kind == HIRTypeKind::Float64 ||
                refined->kind == HIRTypeKind::Bool ||
                refined->kind == HIRTypeKind::String;
            if (!receiverIsObjectLiteral || !refinementIsPrimitive) {
                propType = refined;
            }
        }
    }

    // Private member READ (`obj.#x`): per ECMA-262, accessing a private name on
    // an object that does not have it is a TypeError (brand check), NOT undefined.
    // `obj.#x` and the string key `obj["#x"]` both lower to a "#x" get, so the
    // brand check can't live in the runtime get — emit a distinct private-get.
    // Only when the receiver is statically untyped (Any): a typed `this.#x` is
    // provably an instance of the declaring class, so it keeps the typed
    // createGetPropStatic path (which also unboxes to the field's type).
    if (!node->name.empty() && node->name[0] == '#'
        && obj->type && obj->type->kind == HIRTypeKind::Any) {
        auto keyStr = builder_.createConstString(node->name);
        lastValue_ = builder_.createCall("ts_object_get_private", {obj, keyStr}, HIRType::makeAny());
        return;
    }

    lastValue_ = builder_.createGetPropStatic(obj, node->name, propType);
}

void ASTToHIR::visitObjectLiteralExpression(ast::ObjectLiteralExpression* node) {
    setSourceLine(node);
    // Pre-scan: check if ALL properties are static string names (eligible for flat object)
    HIRShape* flatShape = nullptr;
    bool allStatic = true;
    std::vector<std::string> propNames;

    for (auto& prop : node->properties) {
        if (dynamic_cast<ast::SpreadElement*>(prop.get())) {
            allStatic = false;
            break;
        }
        if (dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            allStatic = false;
            break;
        }
        if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(prop.get())) {
            // Computed property names (e.g. { [expr]: val }) use dynamic keys
            // and can't go into a flat shape — fall back to TsMap.
            if (pa->name.empty() || dynamic_cast<ast::ComputedPropertyName*>(pa->nameNode.get())) {
                allStatic = false;
                break;
            }
            propNames.push_back(pa->name);
        } else if (auto* spa = dynamic_cast<ast::ShorthandPropertyAssignment*>(prop.get())) {
            if (spa->name.empty()) {
                allStatic = false;
                break;
            }
            propNames.push_back(spa->name);
        } else {
            allStatic = false;
            break;
        }
    }

    if (allStatic && !propNames.empty()) {
        auto shape = std::make_shared<HIRShape>();
        shape->id = nextShapeId_++;
        for (uint32_t i = 0; i < (uint32_t)propNames.size(); i++) {
            shape->propertyOffsets[propNames[i]] = i;
        }
        shape->size = 16 + (uint32_t)propNames.size() * 8 + 8;
        flatShape = shape.get();
        module_->shapes.push_back(shape);
    }

    auto obj = builder_.createNewObjectDynamic(flatShape);

    // Inside a generator/async function, sub-expressions of properties can
    // yield. The SSA value of `obj` won't dominate later uses across resume
    // boundaries, so spill to an alloca and reload before each property
    // operation. Same pattern as the array-literal-with-spread fix above.
    bool inGenerator = currentFunction_ && (currentFunction_->isGenerator || currentFunction_->isAsync);
    bool hasYieldableProp = false;
    if (inGenerator) {
        for (auto& prop : node->properties) {
            if (dynamic_cast<ast::SpreadElement*>(prop.get()) ||
                dynamic_cast<ast::PropertyAssignment*>(prop.get()) ||
                dynamic_cast<ast::ComputedPropertyName*>(prop.get())) {
                hasYieldableProp = true;
                break;
            }
        }
    }
    std::shared_ptr<HIRValue> objSlot;
    if (inGenerator && hasYieldableProp) {
        objSlot = builder_.createAlloca(HIRType::makeAny(), "objlit.acc");
        builder_.createStore(obj, objSlot);
    }
    auto reloadObj = [&]() {
        return objSlot ? builder_.createLoad(HIRType::makeAny(), objSlot) : obj;
    };

    for (auto& prop : node->properties) {
        // Handle spread element: {...other}
        if (auto* spread = dynamic_cast<ast::SpreadElement*>(prop.get())) {
            auto spreadObj = lowerExpression(spread->expression.get());
            // Use ts_object_assign to copy properties from spreadObj to obj
            builder_.createCall("ts_object_assign", {reloadObj(), spreadObj}, HIRType::makeAny());
            continue;
        }

        // Handle MethodDefinition (including getters/setters) specially
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(prop.get())) {
            // Create a function for the method
            auto funcValue = lowerMethodDefinitionToFunction(method);

            // Check for computed property name: { [expr]() {} }
            if (auto* computed = dynamic_cast<ast::ComputedPropertyName*>(method->nameNode.get())) {
                if (computed->expression && funcValue) {
                    auto keyVal = lowerExpression(computed->expression.get());
                    // For computed getters/setters we'd need __getter_<dynamic>
                    // which isn't supported. Fall back to a plain dynamic set —
                    // the getter/setter semantics won't fire but the property
                    // will at least exist on the object, preventing crashes.
                    builder_.createSetPropDynamic(reloadObj(), keyVal, funcValue);
                }
            } else {
                // Determine the property key from Identifier or name string
                std::string keyName;
                if (auto* id = dynamic_cast<ast::Identifier*>(method->nameNode.get())) {
                    if (method->isGetter) {
                        keyName = "__getter_" + id->name;
                    } else if (method->isSetter) {
                        keyName = "__setter_" + id->name;
                    } else {
                        keyName = id->name;
                    }
                } else if (!method->name.empty()) {
                    if (method->isGetter) {
                        keyName = "__getter_" + method->name;
                    } else if (method->isSetter) {
                        keyName = "__setter_" + method->name;
                    } else {
                        keyName = method->name;
                    }
                }

                if (!keyName.empty() && funcValue) {
                    builder_.createSetPropStatic(reloadObj(), keyName, funcValue);
                }
            }
        } else {
            // Save the object before visiting property (which may overwrite lastValue_)
            lastValue_ = reloadObj();
            prop->accept(this);
        }
    }

    // Ensure lastValue_ is the object after all properties are set
    lastValue_ = reloadObj();
}

void ASTToHIR::visitPropertyAssignment(ast::PropertyAssignment* node) {
    setSourceLine(node);
    // Save the object before lowerExpression overwrites lastValue_
    auto obj = lastValue_;

    // Inferred name (ECMA-262 PropertyDefinitionEvaluation / NamedEvaluation):
    // { m: function(){} }, { p: () => {} }, { c: class {} } give the value the
    // property key as its .name. Only for a plain (non-computed) key and an
    // anonymous function/arrow/class initializer.
    bool clearPending = false;
    if (!node->name.empty() &&
        !dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
        bool anon = dynamic_cast<ast::ArrowFunction*>(node->initializer.get()) ||
                    dynamic_cast<ast::ClassExpression*>(node->initializer.get());
        if (!anon) {
            if (auto* fe = dynamic_cast<ast::FunctionExpression*>(node->initializer.get()))
                anon = fe->name.empty();
        }
        if (anon) { pendingClosureDisplayName_ = node->name; clearPending = true; }
    }

    auto val = lowerExpression(node->initializer.get());
    if (clearPending) pendingClosureDisplayName_.clear();

    // Check for computed property name: { [expr]: value }
    if (auto* computed = dynamic_cast<ast::ComputedPropertyName*>(node->nameNode.get())) {
        if (computed->expression && obj) {
            auto keyVal = lowerExpression(computed->expression.get());
            builder_.createSetPropDynamic(obj, keyVal, val);
        }
    } else {
        // PropertyAssignment has name (string) directly. The empty string is a
        // valid property key ({ '': v }); computed/spread keys are handled in
        // the branch above, so a non-computed PropertyAssignment with an empty
        // name is an intentional '' key — emit it (previously dropped).
        std::string propName = node->name;
        if (obj) {
            builder_.createSetPropStatic(obj, propName, val);
        }
    }

    // Restore lastValue_ to the object for any subsequent properties
    lastValue_ = obj;
}

void ASTToHIR::visitShorthandPropertyAssignment(ast::ShorthandPropertyAssignment* node) {
    setSourceLine(node);
    // Save the object before any potential modification to lastValue_
    auto obj = lastValue_;

    // Check if this is a captured variable from an outer function first
    // (same logic as visitIdentifier - lookupVariable alone doesn't detect captures)
    std::shared_ptr<HIRValue> val;
    size_t scopeIndex = 0;
    if (currentFunction_ && isCapturedVariable(node->name, &scopeIndex)) {
        auto* info = lookupVariableInfo(node->name);
        if (info) {
            auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
            registerCapture(node->name, type, scopeIndex);
            currentFunction_->hasClosure = true;
            val = builder_.createLoadCapture(node->name, type);
        }
    }
    // Also check module globals (same as visitIdentifier)
    if (!val && currentFunction_ && isModuleGlobalVar(node->name)) {
        size_t si = 0;
        if (isCapturedVariable(node->name, &si)) {
            std::string globalName = modVarName(node->name);
            auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
            val = builder_.createLoadGlobalTyped(globalName, type);
        }
    }
    if (!val)
        val = lookupVariable(node->name);
    if (!val) {
        // Variable not found - check if it's a function name in the module
        for (const auto& func : module_->functions) {
            if (func->name == node->name) {
                // Found a function with this name - load it as a function value
                auto funcType = HIRType::makeFunction();
                funcType->returnType = func->returnType;
                for (const auto& param : func->params) {
                    funcType->paramTypes.push_back(param.second);
                }
                val = builder_.createLoadFunction(node->name, funcType);
                break;
            }
        }

        // Also check specializations - functions might be pending compilation
        if (!val && specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == node->name || spec.specializedName == node->name) {
                    // Found a function declaration - use LoadFunction
                    auto funcType = HIRType::makeFunction();
                    if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                        if (!funcNode->returnType.empty()) {
                            funcType->returnType = convertTypeFromString(funcNode->returnType);
                        }
                        for (const auto& param : funcNode->parameters) {
                            auto paramType = param->type.empty()
                                ? HIRType::makeAny()
                                : convertTypeFromString(param->type);
                            funcType->paramTypes.push_back(paramType);
                        }
                    }
                    val = builder_.createLoadFunction(spec.specializedName, funcType);
                    break;
                }
            }
        }

        // If still not found, create undefined
        if (!val) {
            val = createValue(HIRType::makeAny());
            builder_.createConstUndefined(val);
        }
    }

    if (obj) {
        builder_.createSetPropStatic(obj, node->name, val);
    }

    // Restore lastValue_ to the object for any subsequent properties
    lastValue_ = obj;
}

void ASTToHIR::visitComputedPropertyName(ast::ComputedPropertyName* node) {
    setSourceLine(node);
    if (node->expression) {
        lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitMethodDefinition(ast::MethodDefinition* node) {
    setSourceLine(node);
    // Methods are handled during class lowering
}

void ASTToHIR::visitStaticBlock(ast::StaticBlock* node) {
    setSourceLine(node);
    for (auto& stmt : node->body) {
        lowerStatement(stmt.get());
    }
}

void ASTToHIR::visitIdentifier(ast::Identifier* node) {
    setSourceLine(node);
    if (node->name == "Object" || node->name == "String") {
        SPDLOG_DEBUG("[IDENT-TOP] name={} func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
    }
    // Handle 'this' keyword specially
    if (node->name == "this") {
        // Check if 'this' is a captured variable from an outer function
        // (e.g., arrow functions in class methods capturing lexical this)
        size_t scopeIndex = 0;
        if (currentFunction_ && isCapturedVariable("this", &scopeIndex)) {
            auto* info = lookupVariableInfo("this");
            if (info) {
                auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
                registerCapture("this", type, scopeIndex);
                currentFunction_->hasClosure = true;
                lastValue_ = builder_.createLoadCapture("this", type);
                return;
            }
        }
        // Not captured - look up 'this' in the variable scope
        lastValue_ = lookupVariable("this");
        if (lastValue_) {
            return;
        }
        // If not found in scope, check the dynamic this context
        // (set by Function.prototype.call/apply)
        lastValue_ = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
        return;
    }

    // JavaScript built-in globals must be resolved BEFORE moduleGlobalVars_ check.
    // In untyped JS modules, identifiers like String, Object, Array may appear in
    // moduleGlobalVars_ (from Analyzer function usage tracking) but should resolve
    // to runtime globals, not null module-scoped variables.
    {
        static const std::set<std::string> jsBuiltinGlobals = {
            "Math", "JSON", "Object", "Array", "String", "Number",
            "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
            "process", "global", "globalThis", "Symbol", "Map", "Set",
            "WeakMap", "WeakSet", "Proxy", "Reflect", "Iterator",
            "EvalError", "RangeError", "ReferenceError", "SyntaxError",
            "TypeError", "URIError", "AggregateError", "Function", "console",
            "parseInt", "parseFloat", "isNaN", "isFinite",
            "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
            "setInterval", "clearInterval", "setTimeout", "clearTimeout",
            "setImmediate", "clearImmediate", "queueMicrotask",
            // TypedArray constructors and the %TypedArray% intrinsic
            "TypedArray",
            "Int8Array", "Uint8Array", "Uint8ClampedArray",
            "Int16Array", "Uint16Array",
            "Int32Array", "Uint32Array",
            "Float32Array", "Float64Array",
            "BigInt64Array", "BigUint64Array",
            // Buffer-backed + BigInt + generator-family constructor stubs.
            "ArrayBuffer", "DataView", "SharedArrayBuffer", "BigInt",
            "GeneratorFunction", "AsyncFunction", "AsyncGeneratorFunction",
            // Intl (ECMA-402) namespace
            "Intl",
            // Temporal (TC39) namespace
            "Temporal",
        };
        if (jsBuiltinGlobals.count(node->name)) {
            // A local `function NAME(...) {...}` declaration MUST shadow the
            // built-in global with the same name. Lodash relies on this:
            // `function isNaN(value) {...}` inside its IIFE defines a strict
            // isNaN, and `lodash.isNaN = isNaN` should bind to that local
            // function — not the global ECMAScript isNaN. Without this
            // check `_.isNaN("foo")` returns `true` (global's coercion-
            // based answer) instead of lodash's strict `false`.
            //
            // We only honor FUNCTION-DECLARATION shadows (elemType.kind ==
            // Function), not var/let/const shadows. The lodash bundle also
            // has `var Object = context.Object, Array = context.Array, ...`
            // which hoist to undefined before their assignments execute;
            // honoring those shadows would resolve early `Object` references
            // to undefined and break the bundle.
            auto* localInfo = lookupVariableInfo(node->name);
            bool localIsFunction = localInfo && localInfo->elemType &&
                localInfo->elemType->kind == HIRTypeKind::Function;
            if (!localIsFunction) {
                SPDLOG_DEBUG("[IDENT] builtin global: {} in func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
                lastValue_ = builder_.createLoadGlobal(node->name);
                return;
            }
            SPDLOG_DEBUG("[IDENT] local fn shadows builtin: {} in func={}", node->name, currentFunction_ ? currentFunction_->name : "null");
            // Fall through — local function declaration shadows the built-in.
        }
    }

    // For module-scoped variables, use __modvar_ globals when accessed from inner
    // functions or from the defining function when an inner function also uses it.
    // This ensures the module init function sees updates from closures that modify
    // the variable (e.g., let R = 0; const f = () => { R++; }; f(); exports.R = R).
    if (currentFunction_ && isModuleGlobalVar(node->name)) {
        // Check for a local variable in the CURRENT function's scope first —
        // local declarations (var/let/const or parameters) inside nested
        // functions shadow module globals. We must only check scopes belonging
        // to the current function (not outer functions) because outer function
        // locals aren't accessible via LLVM alloca from a different function.
        {
            bool foundLocal = false;
            for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                auto found = it->variables.find(node->name);
                if (found != it->variables.end() && it->owningFunction == currentFunction_) {
                    // Found in current function's scope — use the local
                    auto* info = &found->second;
                    if (info->isAlloca && info->elemType) {
                        lastValue_ = builder_.createLoad(info->elemType, info->value);
                    } else {
                        lastValue_ = info->value;
                    }
                    foundLocal = true;
                    break;
                }
                // Stop at function boundaries — don't look into outer functions
                if (it->isFunctionBoundary && it->owningFunction != currentFunction_) {
                    break;
                }
            }
            if (foundLocal) {
                // If an inner function references this variable (populated during
                // the hoisted function declaration pass), we must use LoadGlobal
                // instead of the local so we see mutations from closures
                // (e.g., let count = 0; function inc() { count++; }; inc(); console.log(count)).
                // Only applies to __module_init_* functions where variables are true
                // module-level globals, not to user_main or other user functions.
                if (!isModuleGlobalUsedByInner(node->name) ||
                    !currentFunction_ ||
                    currentFunction_->name.find("__module_init_") != 0) return;
                // Fall through to LoadGlobal path below
            }
        }

        size_t scopeIndex = 0;
        if (isCapturedVariable(node->name, &scopeIndex)) {
            // Check: is the variable defined in a non-module-init function?
            // If so, it's a function parameter/local captured by a closure —
            // use LoadCapture, not LoadGlobal. Module globals from __module_init_
            // should use LoadGlobal; function parameters should use captures.
            auto* info = lookupVariableInfo(node->name);
            bool isModuleInitVar = true;
            if (info) {
                // Find the owning function of the variable
                for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
                    if (it->variables.count(node->name)) {
                        if (it->owningFunction &&
                            it->owningFunction->name.find("__module_init_") != 0 &&
                            it->owningFunction->name != "user_main" &&
                            it->owningFunction->name != "__synthetic_user_main") {
                            isModuleInitVar = false;
                        } else if (!it->isFunctionBoundary) {
                            // Block-scoped (e.g. `for (let i ...)`, `if (let x ...)`)
                            // even inside __synthetic_user_main / __module_init_.
                            // Not a true module global — must use closure-capture
                            // mechanism so per-iteration semantics work.
                            isModuleInitVar = false;
                        }
                        break;
                    }
                }
            }

            if (isModuleInitVar) {
                // Module-level variable — use __modvar_ global
                moduleGlobalsUsedByInnerByModule_[node->name].insert(currentModulePath_);
                std::string globalName = modVarName(node->name);
                auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
                lastValue_ = builder_.createLoadGlobalTyped(globalName, type);
                return;
            }
            // Not a module-init var — fall through to the capture path below.
            // Do NOT check moduleGlobalsUsedByInner_ here because that set is
            // global across all modules. A same-named variable in a different
            // module (e.g., `var path = require('path')`) would incorrectly
            // redirect this function-local `path` to LoadGlobal.
        } else {
            // Not a captured variable but name matches a module global —
            // check if the defining function uses it via module global
            if (isModuleGlobalUsedByInner(node->name)) {
                std::string globalName = modVarName(node->name);
                auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
                lastValue_ = builder_.createLoadGlobalTyped(globalName, type);
                return;
            }
        }
    }

    // Check if this is a captured variable from an outer function
    size_t scopeIndex = 0;
    if (currentFunction_ && isCapturedVariable(node->name, &scopeIndex)) {
        // Look up the variable info to get its type
        auto* info = lookupVariableInfo(node->name);
        if (info) {
            auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
            // Register this capture for the current function
            registerCapture(node->name, type, scopeIndex);
            // Mark the function as having closures
            currentFunction_->hasClosure = true;
            // Use LoadCapture for captured variables
            lastValue_ = builder_.createLoadCapture(node->name, type);
            // TDZ: a captured let/const read before its declaration initializes
            // (`function f(){ return x; } f(); let x;`) throws ReferenceError.
            // Any-typed only (see lookupVariable).
            if (info->isTDZ && type->kind == HIRTypeKind::Any) {
                auto nameC = builder_.createConstString(node->name);
                lastValue_ = builder_.createCall("ts_tdz_check",
                    {lastValue_, nameC}, HIRType::makeAny());
            }
            return;
        }
    }

    // Check for local/parameter variables
    lastValue_ = lookupVariable(node->name);
    if (lastValue_) {
        return;
    }

    // Inside a `with` body, any name that did NOT resolve to a local above
    // must consult the with-scope stack at runtime — including the static
    // shortcuts below (builtins, extension registry, NaN/Infinity): the
    // with-object can shadow them (`with({NaN:'x', parseInt(){}}) { NaN }`).
    // typeof keeps its unresolved-yields-undefined semantics.
    if (withDepth_ > 0 && !inTypeofOperand_ && node->name != "undefined") {
        auto nameStr = builder_.createConstString(node->name);
        lastValue_ = builder_.createCall("ts_resolve_identifier_or_throw",
                                         {nameStr}, HIRType::makeAny());
        return;
    }

    // Handle namespace identifiers standalone - these are compile-time constructs
    // with no runtime representation (used only as prefixes for ns.member access).
    // Skip if the name is a registered extension module (path, fs, etc.) - those
    // are handled by the extension registry below via createLoadGlobal.
    // Also skip if this is a CJS module namespace import (stored in moduleGlobalVars_).
    if (node->inferredType && node->inferredType->kind == ts::TypeKind::Namespace) {
        auto& extReg = ext::ExtensionRegistry::instance();
        if (!extReg.isRegisteredGlobalOrModule(node->name) && !isModuleGlobalVar(node->name) &&
            uniqueModuleGlobalName(node->name).empty()) {
            lastValue_ = builder_.createConstUndefined();
            return;
        }
    }

    // Check for JavaScript built-in objects EARLY — before namespace/extension checks.
    // In untyped JS modules, built-ins like String, Object, Array may have
    // incorrect inferred types (Namespace, Any, etc.) that cause them to be
    // resolved as undefined instead of via LoadGlobal.
    {
        static const std::set<std::string> builtinObjects = {
            "Math", "JSON", "Object", "Array", "String", "Number",
            "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
            "process", "global", "globalThis", "Symbol", "Map", "Set",
            "WeakMap", "WeakSet", "Proxy", "Reflect", "Iterator",
            "EvalError", "RangeError", "ReferenceError", "SyntaxError",
            "TypeError", "URIError", "AggregateError", "Function", "Temporal",
        };
        if (builtinObjects.count(node->name)) {
            lastValue_ = builder_.createLoadGlobal(node->name);
            return;
        }
    }

    // Handle special constants first (these are always hardcoded)
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

    // Check ExtensionRegistry for registered objects/modules/globals
    // These include: console, Math, JSON, Object, Array, String, Number, Boolean,
    // Date, RegExp, Promise, Error, Buffer, process, global, globalThis,
    // and Node.js modules like path, fs, os, url, util, crypto, http, https, net, etc.
    auto& registry = ext::ExtensionRegistry::instance();
    if (registry.isRegisteredGlobalOrModule(node->name)) {
        // Emit LoadGlobal for global objects
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
    }

    // Check for known built-in functions used as values (not in call position)
    // These need native function wrappers so they can be passed as callbacks
    static const std::set<std::string> builtinFunctions = {
        "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
        "parseInt", "parseFloat"
    };
    if (builtinFunctions.count(node->name)) {
        auto nameVal = builder_.createConstString(node->name);
        lastValue_ = builder_.createCall("ts_get_builtin_function", {nameVal}, HIRType::makeAny());
        return;
    }

    // Fallback: Check for known JavaScript built-in objects not yet in extension files
    // This maintains backwards compatibility while migrating to registry-based lookups
    static const std::set<std::string> builtinObjects = {
        "Math", "JSON", "Object", "Array", "String", "Number",
        "Boolean", "Date", "RegExp", "Promise", "Error", "Buffer",
        "process", "global", "globalThis"
    };
    if (builtinObjects.count(node->name)) {
        lastValue_ = builder_.createLoadGlobal(node->name);
        return;
    }

    // Check if this is a module-scoped variable from an imported module
    // This must be checked BEFORE module_->functions because imported JS modules
    // compile their functions into module_->functions, but we need to use the
    // imported closure (which has prototype/properties set up) not a fresh one.
    if (isModuleGlobalVar(node->name)) {
        std::string globalName = modVarName(node->name);
        auto type = module_->globals.count(globalName) ? module_->globals[globalName] : HIRType::makeAny();
        lastValue_ = builder_.createLoadGlobalTyped(globalName, type);
        return;
    }

    // Check if this is a function name in the module
    // Functions are declared at module level and can be referenced as values
    for (const auto& func : module_->functions) {
        if (func->name == node->name) {
            // Found a function with this name - load it as a function value
            auto funcType = HIRType::makeFunction();
            funcType->returnType = func->returnType;
            for (const auto& param : func->params) {
                funcType->paramTypes.push_back(param.second);
            }
            lastValue_ = builder_.createLoadFunction(node->name, funcType);
            return;
        }
    }

    // Also check specializations - functions might be pending compilation
    if (specializations_) {
        for (const auto& spec : *specializations_) {
            if (spec.originalName == node->name || spec.specializedName == node->name) {
                // Found a function declaration - use LoadFunction
                auto funcType = HIRType::makeFunction();
                if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                    if (!funcNode->returnType.empty()) {
                        funcType->returnType = convertTypeFromString(funcNode->returnType);
                    }
                    for (const auto& param : funcNode->parameters) {
                        auto paramType = param->type.empty()
                            ? HIRType::makeAny()
                            : convertTypeFromString(param->type);
                        funcType->paramTypes.push_back(paramType);
                    }
                }
                lastValue_ = builder_.createLoadFunction(spec.specializedName, funcType);
                return;
            }
        }
    }

    // Class binding lookup. Two paths reach here:
    //
    // 1. `class E { ... }` (declaration): visitClassDeclaration registers
    //    the class in module_->classes under its name `E`. visitIdentifier
    //    has nothing to load because the constructor function is named
    //    `E_constructor`, not `E`. Find the class by name and load its
    //    constructor.
    //
    // 2. `let B = class { ... }` (expression assigned to a binding):
    //    visitClassExpression registers the class as `__anon_class_N` and
    //    populates `variableToClassName_["B"] = "__anon_class_N"`. The
    //    let-decl statement lives in `module->ast->body` after the
    //    Monomorphizer's keep-class-expr-decls pass, but no later pass
    //    iterates that body to emit the binding store. Resolve `B` via
    //    the map so the binding is virtually present even without an
    //    actual store.
    auto resolveClassByName = [&](const std::string& className) -> bool {
        for (const auto& cls : module_->classes) {
            if (cls->name != className) continue;
            std::string ctorName = cls->constructor
                ? cls->constructor->name
                : cls->name + "_constructor";
            bool hasFn = false;
            for (const auto& f : module_->functions) {
                if (f->name == ctorName) { hasFn = true; break; }
            }
            if (!hasFn && specializations_) {
                for (const auto& spec : *specializations_) {
                    if (spec.specializedName == ctorName) { hasFn = true; break; }
                }
            }
            // The class currently BEING lowered: its ctor function is
            // emitted after the method bodies, so hasFn is still false while
            // a method references the class's own binding (`var C = class {
            // static m() { return C.#x; } }`). LoadFunction is by-name and
            // resolves at LLVM lowering, so a forward reference is fine.
            if (hasFn || (currentClass_ && currentClass_->name == className)) {
                lastValue_ = builder_.createLoadFunction(ctorName);
                return true;
            }
            break;
        }
        return false;
    };
    if (resolveClassByName(node->name)) return;
    auto vtcIt = variableToClassName_.find(node->name);
    if (vtcIt != variableToClassName_.end()) {
        if (resolveClassByName(vtcIt->second)) return;
    }

    // A module-level binding read from a spec-lowered function whose defining
    // scope isn't visible here (class-expression method bodies reference the
    // assigned variable: `var C = class { static m() { return C.#x; } }`).
    // Route through the __modvar_ global, which the module-init assignment
    // stores unconditionally — the previous const-undefined fallback made the
    // method read `C` as undefined (the rs-static-privatename by-classname
    // test262 family).
    if (isModuleGlobalVar(node->name)) {
        moduleGlobalsUsedByInnerByModule_[node->name].insert(currentModulePath_);
        std::string globalName = modVarName(node->name);
        auto gtype = module_->globals.count(globalName)
            ? module_->globals[globalName] : HIRType::makeAny();
        lastValue_ = builder_.createLoadGlobalTyped(globalName, gtype);
        return;
    }
    // Owner-module lookup: a spec-lowered method body's currentModulePath_ may
    // differ from the module that owns the binding. Unambiguous (single-owner)
    // module globals resolve to that module's __modvar_ global.
    {
        std::string uniqueName = uniqueModuleGlobalName(node->name);
        if (!uniqueName.empty()) {
            auto gtype = module_->globals.count(uniqueName)
                ? module_->globals[uniqueName] : HIRType::makeAny();
            lastValue_ = builder_.createLoadGlobalTyped(uniqueName, gtype);
            return;
        }
    }

    // Unresolvable identifier. Throw ReferenceError (ECMA-262 9.4.2 GetValue
    // on an unresolvable Reference) ONLY when the analyzer — which has the
    // complete symbol table (imports, commonjs globals, enums, classes,
    // functions) — also flagged the name as unbound. This gates the throw on
    // the intersection (codegen-fallback-reached AND analyzer-unresolved), so
    // valid bindings that merely slip past codegen's context-dependent
    // resolution still emit undefined (unchanged). `typeof` is exempt
    // (yields "undefined"). The runtime helper throws via a call, not an IR
    // terminator, so it is valid mid-expression.
    // Inside a `with` body every statically-unresolved name must consult the
    // with-scope stack at runtime (the resolver walks it before globalThis).
    if ((node->isUnresolvedReference || withDepth_ > 0) && !inTypeofOperand_) {
        auto nameStr = builder_.createConstString(node->name);
        lastValue_ = builder_.createCall("ts_resolve_identifier_or_throw",
                                         {nameStr}, HIRType::makeAny());
    } else {
        lastValue_ = createValue(HIRType::makeAny());
        builder_.createConstUndefined(lastValue_);
    }
}

void ASTToHIR::visitSuperExpression(ast::SuperExpression* node) {
    setSourceLine(node);
    // TODO: Proper super handling
    lastValue_ = createValue(HIRType::makeObject());
    builder_.createConstNull(lastValue_);
}

void ASTToHIR::visitStringLiteral(ast::StringLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstString(node->value);
}

void ASTToHIR::visitRegularExpressionLiteral(ast::RegularExpressionLiteral* node) {
    setSourceLine(node);
    // Create a RegExp object from the literal text (e.g., "/pattern/flags")
    // The runtime function ts_regexp_from_literal parses the literal and creates the RegExp
    auto literalStr = builder_.createConstString(node->text);
    lastValue_ = builder_.createCall("ts_regexp_from_literal", {literalStr}, HIRType::makeObject());
}

void ASTToHIR::visitNumericLiteral(ast::NumericLiteral* node) {
    setSourceLine(node);
    // In TypeScript/JavaScript, all numbers are IEEE 754 double-precision floats
    lastValue_ = builder_.createConstFloat(node->value);
}

void ASTToHIR::visitBigIntLiteral(ast::BigIntLiteral* node) {
    setSourceLine(node);
    // Strip the 'n' suffix. Input is one of: "123", "0x1F", "0o17", "0b11",
    // each followed by 'n' in the AST token.
    std::string valueStr = node->value;
    if (!valueStr.empty() && valueStr.back() == 'n') {
        valueStr.pop_back();
    }
    // Detect prefix and select the right radix for ts_bigint_create_str.
    // Without this, `0xFEDCBA9876543210n` parses as base 10 and silently
    // clamps / returns 0.
    int radixInt = 10;
    if (valueStr.size() >= 2 && valueStr[0] == '0') {
        char p = valueStr[1];
        if (p == 'x' || p == 'X')      { radixInt = 16; valueStr = valueStr.substr(2); }
        else if (p == 'o' || p == 'O') { radixInt = 8;  valueStr = valueStr.substr(2); }
        else if (p == 'b' || p == 'B') { radixInt = 2;  valueStr = valueStr.substr(2); }
    }
    // Strip ES2021 numeric separators ('_'): ts_bigint_create_str does not
    // understand them and would otherwise parse e.g. "217178610_123_456_789"
    // as 0. Separators only appear between digits, so removing them is safe.
    valueStr.erase(std::remove(valueStr.begin(), valueStr.end(), '_'), valueStr.end());

    // Create the string constant for the BigInt value
    auto strVal = builder_.createConstString(valueStr);

    // Call ts_bigint_create_str. Emit with BigInt type so that downstream
    // binary-op lowering can detect BigInt operands via HIRValue::type,
    // enabling `var a = 1n; var b = 2n; a + b` to pick the BigInt add path.
    auto radix = builder_.createConstInt(radixInt);
    lastValue_ = builder_.createCall("ts_bigint_create_str", {strVal, radix}, HIRType::makeBigInt());
}

void ASTToHIR::visitBooleanLiteral(ast::BooleanLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstBool(node->value);
}

void ASTToHIR::visitNullLiteral(ast::NullLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstNull();
}

void ASTToHIR::visitUndefinedLiteral(ast::UndefinedLiteral* node) {
    setSourceLine(node);
    lastValue_ = builder_.createConstUndefined();
}

void ASTToHIR::visitAwaitExpression(ast::AwaitExpression* node) {
    setSourceLine(node);
    if (node->expression) {
        // Lower the promise expression
        auto promise = lowerExpression(node->expression.get());
        if (!promise) {
            // The inner expression returned void (e.g., calling a function typed as () => void).
            // In JavaScript, all function calls return a value at runtime. When the function
            // is actually a promisified wrapper, it returns a Promise even though the original
            // type says void. Retroactively patch the last Call/CallIndirect instruction to
            // produce an Any-typed result so the await can use it.
            auto* block = builder_.getInsertBlock();
            if (block && !block->instructions.empty()) {
                auto& lastInst = block->instructions.back();
                if ((lastInst->opcode == HIROpcode::Call || lastInst->opcode == HIROpcode::CallIndirect ||
                     lastInst->opcode == HIROpcode::CallMethod) && !lastInst->result) {
                    auto result = builder_.createValue(HIRType::makeAny());
                    lastInst->result = result;
                    promise = result;
                }
            }
            if (!promise) {
                promise = builder_.createConstUndefined();
            }
        }
        // Create await instruction to wait for promise resolution
        lastValue_ = builder_.createAwait(promise);
    } else {
        // await with no expression returns undefined
        lastValue_ = builder_.createConstUndefined();
    }
}

void ASTToHIR::visitYieldExpression(ast::YieldExpression* node) {
    setSourceLine(node);
    // Yield: yield value or yield* iterable
    // yield returns the value passed to next() when generator is resumed
    // yield* delegates to another generator/iterable

    if (node->isAsterisk) {
        // yield* iterable - delegate to another generator
        if (node->expression) {
            auto iterable = lowerExpression(node->expression.get());
            lastValue_ = builder_.createYieldStar(iterable);
        } else {
            // yield* with no expression - undefined behavior, yield undefined
            auto undef = builder_.createConstUndefined();
            lastValue_ = builder_.createYieldStar(undef);
        }
    } else {
        // Regular yield
        if (node->expression) {
            auto value = lowerExpression(node->expression.get());
            lastValue_ = builder_.createYield(value);
        } else {
            // yield with no expression yields undefined
            auto undef = builder_.createConstUndefined();
            lastValue_ = builder_.createYield(undef);
        }
    }

    // GEN-001 Stage 6: annotate the suspension point with the catch-dispatch
    // blocks of the user try scopes armed here (outermost first), so the
    // state-machine lowering can pop those handlers on the suspend edge and
    // re-arm them (same catch targets) on resume. Only scopes belonging to
    // the CURRENT function count — entries from an enclosing function (this
    // body may be lowered inline inside it) live in a different frame.
    if (HIRBlock* ib = builder_.getInsertBlock();
        ib && !ib->instructions.empty()) {
        HIRInstruction* yieldInst = ib->instructions.back().get();
        if (yieldInst->opcode == HIROpcode::Yield ||
            yieldInst->opcode == HIROpcode::YieldStar) {
            for (const auto& [fn, catchTarget] : tryScopeStack_) {
                if (fn == currentFunction_) {
                    yieldInst->tryCatchTargets.push_back(catchTarget);
                }
            }
        }
    }
}

void ASTToHIR::visitDynamicImport(ast::DynamicImport* node) {
    setSourceLine(node);
    // TODO: Dynamic import support
    lastValue_ = createValue(HIRType::makeAny());
    builder_.createConstUndefined(lastValue_);
}


void ASTToHIR::visitTemplateExpression(ast::TemplateExpression* node) {
    setSourceLine(node);
    // Start with the head string
    auto currentStr = builder_.createConstString(node->head);

    for (auto& span : node->spans) {
        // Lower the embedded expression
        auto exprValue = lowerExpression(span.expression.get());

        // Convert to string based on type
        std::shared_ptr<HIRValue> strValue;
        auto exprType = span.expression->inferredType;

        if (exprType && exprType->kind == TypeKind::Int) {
            // Integer to string conversion
            strValue = builder_.createCall("ts_string_from_int", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::Double) {
            // Double to string conversion
            strValue = builder_.createCall("ts_string_from_double", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::Boolean) {
            // Boolean to string conversion
            strValue = builder_.createCall("ts_string_from_bool", {exprValue}, HIRType::makeString());
        } else if (exprType && exprType->kind == TypeKind::String) {
            // Already a string, use directly
            strValue = exprValue;
        } else {
            // For any/boxed types, use runtime coercion to string
            strValue = builder_.createCall("ts_string_from_value", {exprValue}, HIRType::makeString());
        }

        // Concatenate expression result
        currentStr = builder_.createStringConcat(currentStr, strValue);

        // Concatenate the literal part after the expression
        auto litValue = builder_.createConstString(span.literal);
        currentStr = builder_.createStringConcat(currentStr, litValue);
    }

    lastValue_ = currentStr;
}

void ASTToHIR::visitTaggedTemplateExpression(ast::TaggedTemplateExpression* node) {
    setSourceLine(node);
    // Tagged template: tag`str${expr}str...`
    // Calls: tag(stringsArray, ...expressions)
    // stringsArray is an array of the literal parts with a 'raw' property

    if (!node->tag || !node->templateExpr) {
        lastValue_ = builder_.createConstUndefined();
        return;
    }

    // Lower the tag function
    auto tagFn = lowerExpression(node->tag.get());

    // Get template parts - templateExpr could be TemplateExpression or NoSubstitutionTemplateLiteral
    std::vector<std::string> stringParts;
    std::vector<std::shared_ptr<HIRValue>> expressions;

    auto* templateExpr = dynamic_cast<ast::TemplateExpression*>(node->templateExpr.get());
    if (templateExpr) {
        // Template with substitutions
        stringParts.push_back(templateExpr->head);

        for (const auto& span : templateExpr->spans) {
            if (span.expression) {
                expressions.push_back(lowerExpression(span.expression.get()));
            }
            stringParts.push_back(span.literal);
        }
    } else {
        // NoSubstitutionTemplateLiteral - just a single string
        auto* strLit = dynamic_cast<ast::StringLiteral*>(node->templateExpr.get());
        if (strLit) {
            stringParts.push_back(strLit->value);
        }
    }

    // Create the strings array with the proper elements
    auto arrayLen = builder_.createConstInt(static_cast<int64_t>(stringParts.size()));
    auto stringsArray = builder_.createNewArrayBoxed(arrayLen, HIRType::makeString());
    for (size_t i = 0; i < stringParts.size(); ++i) {
        auto idx = builder_.createConstInt(static_cast<int64_t>(i));
        auto strVal = builder_.createConstString(stringParts[i]);
        builder_.createSetElem(stringsArray, idx, strVal);
    }

    // Add 'raw' property to the strings array (same values for now)
    // TODO: Handle raw string escapes properly (e.g., `\n` vs actual newline)
    auto rawArray = builder_.createNewArrayBoxed(arrayLen, HIRType::makeString());
    for (size_t i = 0; i < stringParts.size(); ++i) {
        auto idx = builder_.createConstInt(static_cast<int64_t>(i));
        auto strVal = builder_.createConstString(stringParts[i]);
        builder_.createSetElem(rawArray, idx, strVal);
    }
    builder_.createSetPropStatic(stringsArray, "raw", rawArray);

    // Build argument list: [stringsArray, ...expressions]
    std::vector<std::shared_ptr<HIRValue>> args;
    args.push_back(stringsArray);
    for (const auto& expr : expressions) {
        args.push_back(expr);
    }

    // Call the tag function with indirect call (since tag could be any callable)
    lastValue_ = builder_.createCallIndirect(tagFn, args, HIRType::makeAny());
}

void ASTToHIR::visitAsExpression(ast::AsExpression* node) {
    setSourceLine(node);
    // Type assertion - just lower the expression
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitNonNullExpression(ast::NonNullExpression* node) {
    setSourceLine(node);
    // Non-null assertion - just lower the expression
    if (node->expression) {
        lastValue_ = lowerExpression(node->expression.get());
    }
}

void ASTToHIR::visitPrefixUnaryExpression(ast::PrefixUnaryExpression* node) {
    setSourceLine(node);
    // `typeof <unresolvable identifier>` must yield "undefined", not throw
    // ReferenceError (ECMA-262 13.5.1.1). Suppress the unresolvable-reference
    // throw while lowering a typeof operand.
    bool savedTypeofFlag = inTypeofOperand_;
    if (node->op == "typeof") inTypeofOperand_ = true;
    auto operand = lowerExpression(node->operand.get());
    inTypeofOperand_ = savedTypeofFlag;

    const std::string& op = node->op;
    if (op == "-") {
        // BigInt operand: route through ts_bigint_neg. Otherwise the
        // generic Neg op below would treat the NaN-boxed pointer as an
        // i64 and produce nonsense (e.g. `-(0n)` becomes a number with
        // value INT64_MIN as a double).
        bool isBigInt = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::BigInt) {
            isBigInt = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::BigInt) {
            isBigInt = true;
        }
        if (isBigInt) {
            lastValue_ = builder_.createCall("ts_bigint_neg", {operand},
                HIRType::makeBigInt());
        } else if (operand && operand->type &&
                   (operand->type->kind == HIRTypeKind::Any ||
                    operand->type->kind == HIRTypeKind::Object ||
                    operand->type->kind == HIRTypeKind::String)) {
            // Dynamic operand: ts_value_neg implements ES 13.5.5 ToNumeric --
            // a runtime BigInt negates as a BigInt (NegF64's get_double read
            // a dynamic BigInt as 0), everything else as -ToNumber.
            lastValue_ = builder_.createCall("ts_value_neg",
                {boxValueIfNeeded(operand)}, HIRType::makeAny());
        } else {
            // Strategy B Phase 3: emit generic Neg. SpecializationPass will
            // rewrite to NegF64 or NegI64 based on the result type. Keeps the
            // AST-fallback helper logic local to ASTToHIR until Phase 4.
            // JS unary minus ALWAYS produces a Number (an IEEE double). Keep an
            // Int64 result only when the operand is statically an integer or
            // boolean (a real optimization for integer arithmetic); for every
            // other operand type — Float64, Any, String, Object/wrapper, or
            // untyped — use Float64. Typing the Neg result Int64 for those is
            // wrong two ways: (1) it truncates/garbles when the result is boxed
            // by HIR type (e.g. a ternary branch `cond ? 0 : -v` with a
            // fractional `v`), and (2) SpecializationPass cannot lower a
            // Neg(result=i64, operand=string) at all → the value never lands in
            // valueMap_ and the use reads garbage/0 (e.g. `-'2'` yielded 0).
            // NegF64 calls ts_value_get_double, which ToNumber-coerces ints,
            // numeric strings, and Number/wrapper objects correctly.
            bool isInt = false;
            if (operand && operand->type &&
                (operand->type->kind == HIRTypeKind::Int64 ||
                 operand->type->kind == HIRTypeKind::Bool)) {
                isInt = true;
            }
            auto resultType = isInt ? HIRType::makeInt64() : HIRType::makeFloat64();
            lastValue_ = builder_.createNeg(operand, resultType);
        }
    } else if (op == "!") {
        lastValue_ = builder_.createLogicalNot(operand);
    } else if (op == "~") {
        // ES 13.5.6: ToNumeric then bitwise NOT. Statically numeric operands
        // keep the raw i64 form; everything else (bool, string, Any, object,
        // BigInt) goes through the coercing runtime dispatcher — NotI64 on a
        // raw i1/pointer is garbage (`~true` must be -2, `~"1"` must be -2,
        // `~1n` must be the BigInt NOT).
        {
            bool isNumeric = operand && operand->type &&
                (operand->type->kind == HIRTypeKind::Int64 ||
                 operand->type->kind == HIRTypeKind::Float64);
            if (isNumeric) {
                lastValue_ = builder_.createNotI64(operand);
            } else {
                lastValue_ = builder_.createCall("ts_value_bitnot",
                    {boxValueIfNeeded(operand)}, HIRType::makeAny());
            }
        }
    } else if (op == "+") {
        // Unary plus (ES 13.5.4): no-op for statically numeric operands,
        // full ToNumber for everything else. A Bool operand must NOT pass
        // through unchanged (`+false` must be the NUMBER 0, not `false`) —
        // route it with string/Any/object through ts_value_pos, which also
        // throws the spec TypeError for a BigInt operand.
        bool isNumeric = false;
        if (operand && operand->type) {
            auto k = operand->type->kind;
            isNumeric = (k == HIRTypeKind::Int64 || k == HIRTypeKind::Float64);
        }
        if (isNumeric) {
            lastValue_ = operand;
        } else {
            lastValue_ = builder_.createCall("ts_value_pos",
                {boxValueIfNeeded(operand)}, HIRType::makeAny());
        }
    } else if (op == "typeof") {
        lastValue_ = builder_.createTypeOf(operand);
    } else if (op == "++" || op == "--") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }
        // Check if operand is Any type (NaN-boxed) - need runtime dispatch
        bool isAny = false;
        if (!isFloat && operand && operand->type && operand->type->kind == HIRTypeKind::Any) {
            isAny = true;
        }

        std::shared_ptr<HIRValue> result;
        if (isAny) {
            // For NaN-boxed values, use ts_value_inc/dec which coerce to number
            // (unlike ts_value_add which does string concatenation for strings)
            result = (op == "++") ? builder_.createCall("ts_value_inc", {operand}, HIRType::makeAny())
                                  : builder_.createCall("ts_value_dec", {operand}, HIRType::makeAny());
        } else if (isFloat) {
            auto one = builder_.createConstFloat(1.0);
            result = (op == "++") ? builder_.createAddF64(operand, one)
                                  : builder_.createSubF64(operand, one);
        } else {
            // Coerce bool operand to i64 (ToNumber: false=0, true=1) so the
            // i64 add/sub doesn't get a type-mismatched i1 LHS.
            if (operand && operand->type && operand->type->kind == HIRTypeKind::Bool) {
                operand = builder_.createCastBoolToI64(operand);
            }
            auto one = builder_.createConstInt(1);
            result = (op == "++") ? builder_.createAddI64(operand, one)
                                  : builder_.createSubI64(operand, one);
        }

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            // For module-scoped variables from inner functions, use __modvar_ globals
            bool handledAsModGlobal = false;
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                bool captured = isCapturedVariable(ident->name, &scopeIdx);
                // Write back to the module global whenever the read resolved to
                // it: either the var is captured from an outer scope, OR there is
                // no local binding in this function. The latter covers class
                // methods lowered as standalone specs, where a top-level
                // `callCount` is read via __modvar_ but is not an in-scope
                // capture — without this, `callCount++` reads the global and
                // silently drops the store (ECMA-262 §13.4 requires
                // read+increment+write to the same location).
                bool hasLocal = lookupVariableInfoInCurrentFunction(ident->name) != nullptr;
                if (captured || !hasLocal) {
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                    handledAsModGlobal = true;
                }
            }
            if (!handledAsModGlobal) {
                // Check if this is a captured variable from an outer function
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    // Store to captured variable
                    auto* info = lookupVariableInfo(ident->name);
                    auto type = info && info->elemType ? info->elemType : result->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, result);
                } else {
                    auto* info = lookupVariableInfo(ident->name);
                    if (info && info->isAlloca) {
                        builder_.createStore(result, info->value, info->elemType);
                        broadcastCaptureWrite(info, result);
                        // If used by inner function AND module global, also update __modvar_
                        if (isModuleGlobalUsedByInner(ident->name)) {
                            builder_.createStoreGlobal(modVarName(ident->name), result);
                        }
                    } else {
                        defineVariable(ident->name, result);
                    }
                }
            }
        }
        // Handle property access (e.g., this.#count++ or obj.field++)
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get());
        if (prop) {
            // Same ClassName.staticField fast-path as the postfix variant.
            // ECMA-262 §13.4 UpdateExpression: read, increment, write — the
            // write must target the same storage location as the read; for a
            // static class field that's the per-class _static_ LLVM global.
            bool storedToStaticGlobal = false;
            if (auto* classNameIdent = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                for (auto& cls : module_->classes) {
                    if (cls->name == classNameIdent->name) {
                        std::string globalName = cls->name + "_static_" + prop->name;
                        auto it = staticPropertyGlobals_.find(globalName);
                        if (it != staticPropertyGlobals_.end()) {
                            builder_.createStore(result, it->second.first, it->second.second);
                            // Mirror the updated value onto the constructor
                            // closure so a non-literal reference reads it.
                            auto ctorVal = builder_.createLoadFunction(cls->name + "_constructor");
                            builder_.createSetPropStatic(ctorVal, privateStorageKey(prop->name), result);
                            storedToStaticGlobal = true;
                        }
                        break;
                    }
                }
            }
            if (!storedToStaticGlobal) {
                auto obj = lowerExpression(prop->expression.get());
                std::string propName = prop->name;
                builder_.createSetPropStatic(obj, propName, result);
            }
        }
        // Handle element access (e.g., obj[key]++, arr[i]++)
        auto* elem = dynamic_cast<ast::ElementAccessExpression*>(node->operand.get());
        if (elem) {
            auto obj = lowerExpression(elem->expression.get());
            auto key = lowerExpression(elem->argumentExpression.get());
            builder_.createSetPropDynamic(obj, key, result);
        }
        lastValue_ = result;  // Prefix returns new value
    } else {
        lastValue_ = operand;
    }
}

void ASTToHIR::visitDeleteExpression(ast::DeleteExpression* node) {
    setSourceLine(node);
    // Inside a `with` body, `delete name` on a bare identifier deletes the
    // binding from the innermost with-object that has it (ES 13.5.1 -> the
    // object Environment Record's DeleteBinding); otherwise sloppy-mode
    // semantics (delete of an unresolvable reference yields true).
    if (withDepth_ > 0) {
        if (auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get())) {
            auto nameStr = builder_.createConstString(ident->name);
            lastValue_ = builder_.createCall("ts_with_delete", {nameStr},
                                             HIRType::makeAny());
            return;
        }
    }
    // Handle delete obj.prop or delete obj["prop"]
    if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        // `delete new.target` (possibly parenthesized): not a Reference —
        // per ES 13.5.1 delete of a non-reference evaluates the operand and
        // returns true.
        if (propAccess->name == "target") {
            if (auto* baseId = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
                baseId && baseId->name == "new") {
                lastValue_ = builder_.createConstBool(true);
                return;
            }
        }
        // delete obj.prop — use DeleteProp HIR opcode so the lowering goes
        // through lowerDeleteProp → getTsObjectDeleteProperty (correct i32
        // return type). The generic createCall path declared the function
        // with ptr return type, causing it to be silently unlinked.
        auto obj = lowerExpression(propAccess->expression.get());
        auto key = builder_.createConstString(propAccess->name);
        lastValue_ = builder_.createDeleteProp(obj, key);
        return;
    }

    if (auto* elemAccess = dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())) {
        // delete obj["prop"] or delete obj[key]
        auto obj = lowerExpression(elemAccess->expression.get());
        auto key = lowerExpression(elemAccess->argumentExpression.get());
        lastValue_ = builder_.createDeleteProp(obj, key);
        return;
    }

    // For other cases (like delete x), just return true
    // JavaScript spec says delete on non-references returns true
    lastValue_ = builder_.createConstBool(true);
}

void ASTToHIR::visitPostfixUnaryExpression(ast::PostfixUnaryExpression* node) {
    setSourceLine(node);
    auto operand = lowerExpression(node->operand.get());
    auto oldValue = operand;

    const std::string& op = node->op;
    if (op == "++" || op == "--") {
        // Determine if operand is floating point
        bool isFloat = false;
        if (operand && operand->type && operand->type->kind == HIRTypeKind::Float64) {
            isFloat = true;
        } else if (node->operand->inferredType && node->operand->inferredType->kind == ts::TypeKind::Double) {
            isFloat = true;
        }
        // Check if operand is Any type (NaN-boxed) - need runtime dispatch
        bool isAny = false;
        if (!isFloat && operand && operand->type && operand->type->kind == HIRTypeKind::Any) {
            isAny = true;
        }

        std::shared_ptr<HIRValue> result;
        if (isAny) {
            // For NaN-boxed values, use ts_value_inc/dec which coerce to number
            // (unlike ts_value_add which does string concatenation for strings)
            result = (op == "++") ? builder_.createCall("ts_value_inc", {operand}, HIRType::makeAny())
                                  : builder_.createCall("ts_value_dec", {operand}, HIRType::makeAny());
        } else if (isFloat) {
            auto one = builder_.createConstFloat(1.0);
            result = (op == "++") ? builder_.createAddF64(operand, one)
                                  : builder_.createSubF64(operand, one);
        } else {
            // Coerce bool operand to i64 (ToNumber: false=0, true=1) so the
            // i64 add/sub doesn't get a type-mismatched i1 LHS.
            if (operand && operand->type && operand->type->kind == HIRTypeKind::Bool) {
                operand = builder_.createCastBoolToI64(operand);
                oldValue = operand;
            }
            auto one = builder_.createConstInt(1);
            result = (op == "++") ? builder_.createAddI64(operand, one)
                                  : builder_.createSubI64(operand, one);
        }

        // Update variable if operand is an identifier
        auto* ident = dynamic_cast<ast::Identifier*>(node->operand.get());
        if (ident) {
            // For module-scoped variables from inner functions, use __modvar_ globals
            bool handledAsModGlobal = false;
            if (currentFunction_ && isModuleGlobalVar(ident->name)) {
                size_t scopeIdx = 0;
                bool captured = isCapturedVariable(ident->name, &scopeIdx);
                // Write back to the module global whenever the read resolved to
                // it: either the var is captured from an outer scope, OR there is
                // no local binding in this function. The latter covers class
                // methods lowered as standalone specs, where a top-level
                // `callCount` is read via __modvar_ but is not an in-scope
                // capture — without this, `callCount++` reads the global and
                // silently drops the store (ECMA-262 §13.4 requires
                // read+increment+write to the same location).
                bool hasLocal = lookupVariableInfoInCurrentFunction(ident->name) != nullptr;
                if (captured || !hasLocal) {
                    builder_.createStoreGlobal(modVarName(ident->name), result);
                    handledAsModGlobal = true;
                }
            }
            if (!handledAsModGlobal) {
                // Check if this is a captured variable from an outer function
                size_t scopeIndex = 0;
                if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
                    // Store to captured variable
                    auto* info = lookupVariableInfo(ident->name);
                    auto type = info && info->elemType ? info->elemType : result->type;
                    registerCapture(ident->name, type, scopeIndex);
                    currentFunction_->hasClosure = true;
                    builder_.createStoreCapture(ident->name, result);
                } else {
                    auto* info = lookupVariableInfo(ident->name);
                    if (info && info->isAlloca) {
                        builder_.createStore(result, info->value, info->elemType);
                        broadcastCaptureWrite(info, result);
                        // If used by inner function AND module global, also update __modvar_
                        if (isModuleGlobalUsedByInner(ident->name)) {
                            builder_.createStoreGlobal(modVarName(ident->name), result);
                        }
                    } else {
                        defineVariable(ident->name, result);
                    }
                }
            }
        }
        // Handle property access (e.g., this.#count++ or obj.field++)
        auto* prop = dynamic_cast<ast::PropertyAccessExpression*>(node->operand.get());
        if (prop) {
            // Static class field: ClassName.field++. Mirrors the regular
            // assignment path (visitAssignmentExpression) which routes writes
            // for `ClassName.field = ...` through the per-class
            // `<ClassName>_static_<field>` LLVM global rather than the dynamic
            // property setter. Without this branch, postfix `++` reads from
            // the global (correctly) but `createSetPropStatic` writes only to
            // the class object's dynamic property map, so subsequent reads
            // through the same global stay at the old value.
            bool storedToStaticGlobal = false;
            if (auto* classNameIdent = dynamic_cast<ast::Identifier*>(prop->expression.get())) {
                for (auto& cls : module_->classes) {
                    if (cls->name == classNameIdent->name) {
                        std::string globalName = cls->name + "_static_" + prop->name;
                        auto it = staticPropertyGlobals_.find(globalName);
                        if (it != staticPropertyGlobals_.end()) {
                            builder_.createStore(result, it->second.first, it->second.second);
                            // Mirror onto the constructor closure so a
                            // non-literal reference reads the updated value.
                            auto ctorVal = builder_.createLoadFunction(cls->name + "_constructor");
                            builder_.createSetPropStatic(ctorVal, privateStorageKey(prop->name), result);
                            storedToStaticGlobal = true;
                        }
                        break;
                    }
                }
            }
            if (!storedToStaticGlobal) {
                auto obj = lowerExpression(prop->expression.get());
                std::string propName = prop->name;
                builder_.createSetPropStatic(obj, propName, result);
            }
        }
        // Handle element access (e.g., obj[key]++, arr[i]++)
        auto* elem = dynamic_cast<ast::ElementAccessExpression*>(node->operand.get());
        if (elem) {
            auto obj = lowerExpression(elem->expression.get());
            auto key = lowerExpression(elem->argumentExpression.get());
            builder_.createSetPropDynamic(obj, key, result);
        }
        // Postfix returns old value
        lastValue_ = oldValue;
    } else {
        lastValue_ = operand;
    }
}



}  // namespace ts::hir
