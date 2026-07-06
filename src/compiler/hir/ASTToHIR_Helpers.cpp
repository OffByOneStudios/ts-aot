#include "ASTToHIR_Internal.h"

namespace ts::hir {


std::shared_ptr<HIRValue> ASTToHIR::createValue(std::shared_ptr<HIRType> type) {
    // Delegate to builder to ensure we use the same value counter as HIRFunction
    return builder_.createValue(type);
}

HIRBlock* ASTToHIR::createBlock(const std::string& hint) {
    std::ostringstream ss;
    ss << hint << blockCounter_++;
    return currentFunction_->createBlock(ss.str());
}

void ASTToHIR::pushScope() {
    Scope scope;
    scope.isFunctionBoundary = false;
    scope.owningFunction = currentFunction_;
    scopes_.push_back(scope);
}

void ASTToHIR::pushFunctionScope(HIRFunction* func) {
    Scope scope;
    scope.isFunctionBoundary = true;
    scope.owningFunction = func;
    scopes_.push_back(scope);
}

void ASTToHIR::popScope() {
    if (!scopes_.empty()) {
        SPDLOG_DEBUG("[SCOPE] pop depth={} isFuncBoundary={} owner={}",
            scopes_.size(),
            scopes_.back().isFunctionBoundary,
            scopes_.back().owningFunction ? scopes_.back().owningFunction->name : "null");
        scopes_.pop_back();
    } else {
        SPDLOG_ERROR("[SCOPE] popScope called on EMPTY scope stack!");
    }
}

void ASTToHIR::emitMutualRecursionFixup() {
    if (innerFuncClosures_.size() <= 1) {
        innerFuncClosures_.clear();
        return;
    }

    // Collect the set of inner function names in this scope
    std::set<std::string> innerFuncNames;
    for (const auto& info : innerFuncClosures_) {
        innerFuncNames.insert(info.funcName);
    }

    // For each closure, update cells that reference sibling functions
    for (const auto& info : innerFuncClosures_) {
        for (const auto& [capName, capIdx] : info.captureNamesAndIndices) {
            // Skip self-references (handled by existing LLVM-level fix)
            if (capName == info.funcName) continue;

            // If this capture names a sibling inner function, update the cell
            if (innerFuncNames.count(capName)) {
                auto* siblingInfo = lookupVariableInfo(capName);
                if (siblingInfo && siblingInfo->isAlloca) {
                    auto currentVal = builder_.createLoad(
                        siblingInfo->elemType ? siblingInfo->elemType : HIRType::makeAny(),
                        siblingInfo->value);
                    builder_.createStoreCaptureFromClosure(
                        info.closureValue, capIdx, currentVal);
                }
            }
        }
    }
    innerFuncClosures_.clear();
}

void ASTToHIR::defineVariable(const std::string& name, std::shared_ptr<HIRValue> value) {
    if (!scopes_.empty()) {
        VariableInfo info;
        info.value = value;
        info.isAlloca = false;
        info.elemType = nullptr;
        scopes_.back().variables[name] = info;
    }
}

void ASTToHIR::defineVariableAlloca(const std::string& name, std::shared_ptr<HIRValue> allocaPtr,
                                     std::shared_ptr<HIRType> elemType) {
    if (!scopes_.empty()) {
        VariableInfo info;
        info.value = allocaPtr;
        info.isAlloca = true;
        info.elemType = elemType;
        scopes_.back().variables[name] = info;
    }
}

// Broadcast a write to every closure cell that captures this variable. The
// primary cell (info.closurePtr / info.captureIndex) is updated first, then
// each entry in info.additionalCaptures. Without this, when multiple nested
// closures capture the same variable (lodash captures `upperFirst` from
// many helpers), an assignment to the var would only update the first
// closure's cell, leaving subsequent ones holding stale values.
void ASTToHIR::broadcastCaptureWrite(VariableInfo* info,
                                     std::shared_ptr<HIRValue> newValue) {
    if (!info || !info->isCapturedByNested) return;
    if (info->closurePtr && info->captureIndex >= 0) {
        auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
        builder_.createStoreCaptureFromClosure(closureVal, info->captureIndex, newValue);
    }
    for (const auto& cap : info->additionalCaptures) {
        if (cap.first && cap.second >= 0) {
            auto closureVal = builder_.createLoad(HIRType::makeAny(), cap.first);
            builder_.createStoreCaptureFromClosure(closureVal, cap.second, newValue);
        }
    }
}

ASTToHIR::VariableInfo* ASTToHIR::lookupVariableInfo(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

ASTToHIR::VariableInfo* ASTToHIR::lookupVariableInfoInCurrentFunction(const std::string& name) {
    // Search scopes only within the current function (stop at function boundaries
    // that belong to a different function). This prevents a `var` declaration in a
    // nested function from finding and overwriting an outer function's alloca.
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            // Safety: if we found the variable but it's in a scope owned by a
            // different function, don't return it. This prevents a local `var`
            // declaration from finding a same-named variable from an outer
            // function's scope (e.g., `var url` in parseurl shadowing module-level
            // `var url = require('url')`). Without this check, the function-local
            // var stores to the outer function's alloca, which can be null or
            // point to a destroyed stack frame.
            if (it->isFunctionBoundary && it->owningFunction && it->owningFunction != currentFunction_) {
                return nullptr;
            }
            return &found->second;
        }
        // Stop at function boundaries belonging to a different function
        if (it->isFunctionBoundary && it->owningFunction != currentFunction_) {
            break;
        }
    }
    return nullptr;
}

std::shared_ptr<HIRValue> ASTToHIR::lookupVariable(const std::string& name) {
    // Legacy method - looks up and emits load if needed
    auto* info = lookupVariableInfo(name);
    if (!info) return nullptr;

    // If this variable is captured by a nested closure, we need to read from the cell
    if (info->isCapturedByNested && info->closurePtr && info->captureIndex >= 0) {
        // Use cell-based access: ts_closure_get_cell(closure, index) -> ts_cell_get(cell)
        auto type = info->elemType ? info->elemType : HIRType::makeAny();
        // closurePtr is an alloca - load the closure pointer first to ensure dominance
        auto closureVal = builder_.createLoad(HIRType::makeAny(), info->closurePtr);
        // Pass the original variable value as fallback for paths where the closure
        // was never created (e.g., closure only in one branch of if/else)
        std::shared_ptr<HIRValue> fallback = nullptr;
        if (info->isAlloca && info->value) {
            fallback = builder_.createLoad(info->elemType ? info->elemType : type, info->value);
        } else if (info->value) {
            fallback = info->value;
        }
        auto cellVal = builder_.createLoadCaptureFromClosure(closureVal, info->captureIndex, type, fallback);
        if (info->isTDZ && type->kind == HIRTypeKind::Any) {
            auto nameC = builder_.createConstString(name);
            cellVal = builder_.createCall("ts_tdz_check", {cellVal, nameC}, HIRType::makeAny());
        }
        return cellVal;
    }

    if (info->isAlloca && info->elemType) {
        // Emit a load for alloca-stored variables
        auto loaded = builder_.createLoad(info->elemType, info->value);
        // TDZ: a pre-declared let/const read before its declaration holds the
        // sentinel; ts_tdz_check throws ReferenceError (no-op afterwards).
        // Any-typed slots only: once the declaration narrows elemType to a
        // typed form the value is provably initialized, and wrapping a typed
        // load in the Any-returning check corrupted typed code (golden-ir).
        if (info->isTDZ && info->elemType->kind == HIRTypeKind::Any) {
            auto nameC = builder_.createConstString(name);
            loaded = builder_.createCall("ts_tdz_check", {loaded, nameC}, HIRType::makeAny());
        }
        return loaded;
    }
    return info->value;
}

bool ASTToHIR::isCapturedVariable(const std::string& name, size_t* outScopeIndex) {
    // Search from innermost to outermost scope
    // A variable is captured if it's defined in a scope that belongs to a DIFFERENT function.
    // We use owningFunction to check this, rather than counting function boundaries,
    // because block scopes (if/for/etc.) within a function are not function boundaries
    // but still belong to the outer function.
    size_t scopeIndex = scopes_.size();

    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it, --scopeIndex) {
        auto found = it->variables.find(name);
        if (found != it->variables.end()) {
            // Found the variable - is it from a different function?
            if (it->owningFunction != currentFunction_) {
                if (outScopeIndex) *outScopeIndex = scopeIndex - 1;
                return true;
            }
            return false;
        }
    }

    return false;  // Variable not found
}

void ASTToHIR::registerCapture(const std::string& name, std::shared_ptr<HIRType> type, size_t scopeIndex) {
    // Check if already registered
    for (const auto& cap : pendingCaptures_) {
        if (cap.name == name) return;  // Already captured
    }

    CaptureInfo info;
    info.name = name;
    info.type = type;
    info.outerScopeIndex = scopeIndex;
    pendingCaptures_.push_back(info);
}

//==============================================================================
// Control Flow Helpers
//==============================================================================

void ASTToHIR::emitBranchIfNeeded(HIRBlock* target) {
    if (!hasTerminator()) {
        builder_.createBranch(target);
    }
}

bool ASTToHIR::hasTerminator() {
    HIRBlock* block = builder_.getInsertBlock();
    if (!block || block->instructions.empty()) {
        return false;
    }
    auto& last = block->instructions.back();
    // Check if last instruction is a terminator
    auto op = last->opcode;
    return op == HIROpcode::Branch || op == HIROpcode::CondBranch ||
           op == HIROpcode::Return || op == HIROpcode::ReturnVoid ||
           op == HIROpcode::Throw || op == HIROpcode::Unreachable;
}

std::shared_ptr<HIRValue> ASTToHIR::boxValueIfNeeded(std::shared_ptr<HIRValue> value) {
    // If value is already Any/ptr type, no boxing needed
    if (!value->type || value->type->kind == HIRTypeKind::Any ||
        value->type->kind == HIRTypeKind::Ptr) {
        return value;
    }

    // Box based on value type
    switch (value->type->kind) {
        case HIRTypeKind::Int64:
            return builder_.createBoxInt(value);
        case HIRTypeKind::Float64:
            return builder_.createBoxFloat(value);
        case HIRTypeKind::Bool:
            return builder_.createBoxBool(value);
        case HIRTypeKind::String:
            return builder_.createBoxString(value);
        case HIRTypeKind::Object:
        case HIRTypeKind::Array:
        case HIRTypeKind::Function:
        case HIRTypeKind::Class:
            return builder_.createBoxObject(value);
        default:
            // Already a ptr-like type, return as is
            return value;
    }
}

std::shared_ptr<HIRValue> ASTToHIR::forceBoxValue(std::shared_ptr<HIRValue> value) {
    // Force boxing regardless of the current type
    // This is needed for cases where the type at HIR level might be Any
    // but after inlining the actual value could be an unboxed primitive
    if (!value->type) {
        return value;  // No type info, return as-is
    }

    switch (value->type->kind) {
        case HIRTypeKind::Int64:
            return builder_.createBoxInt(value);
        case HIRTypeKind::Float64:
            return builder_.createBoxFloat(value);
        case HIRTypeKind::Bool:
            return builder_.createBoxBool(value);
        case HIRTypeKind::String:
            return builder_.createBoxString(value);
        case HIRTypeKind::Object:
        case HIRTypeKind::Array:
        case HIRTypeKind::Function:
        case HIRTypeKind::Class:
            return builder_.createBoxObject(value);
        case HIRTypeKind::Any:
        case HIRTypeKind::Ptr:
            // Type says it's already a pointer, but after inlining it might not be
            // Use runtime check: ts_ensure_boxed will check and box if needed
            return builder_.createCall("ts_ensure_boxed", {value}, HIRType::makeAny());
        default:
            return value;
    }
}

//==============================================================================
// Parameter Binder Helpers (Strategy B Phase 6)
//==============================================================================

void ASTToHIR::preseedParamTDZ(HIRFunction* func,
                               const std::vector<std::unique_ptr<ast::Parameter>>& astParams) {
    bool anyDefault = false;
    for (auto& ap : astParams)
        if (ap && ap->initializer) { anyDefault = true; break; }
    if (!anyDefault) return;
    for (size_t i = 0; i < func->params.size(); ++i) {
        const auto& [pn, pt] = func->params[i];
        if (pn == "this" || pn == "__closure__") continue;
        if (pn.rfind("__arg", 0) == 0) continue;
        if (!pt || pt->kind != HIRTypeKind::Any) continue;
        auto a = builder_.createAlloca(HIRType::makeAny(), pn);
        auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
        builder_.createStore(tdz, a, HIRType::makeAny());
        defineVariableAlloca(pn, a, HIRType::makeAny());
        if (auto* vi = lookupVariableInfoInCurrentFunction(pn)) vi->isTDZ = true;
    }
}

void ASTToHIR::bindOneParameter(HIRFunction* func,
                                size_t hirParamIndex,
                                ast::Parameter* astParam,
                                bool useAlloca) {
    const auto& [paramName, paramType] = func->params[hirParamIndex];
    auto paramValue = std::make_shared<HIRValue>(
        static_cast<uint32_t>(hirParamIndex), paramType, paramName);

    if (astParam && astParam->initializer) {
        // Parameter has a default value - check if undefined and use default.
        // We can't use pointer comparison because ts_value_make_undefined()
        // creates a new TsValue* each time, so pointers won't match. Instead
        // use ts_value_is_undefined() which checks the type field.
        auto allocaVal = builder_.createAlloca(paramType);

        auto isUndefined = builder_.createCall("ts_value_is_undefined",
            {paramValue}, HIRType::makeBool());

        auto defaultBB = func->createBlock("default_param");
        auto usedBB = func->createBlock("use_param");
        auto mergeBB = func->createBlock("param_merge");

        builder_.createCondBranch(isUndefined, defaultBB, usedBB);

        // Default block - evaluate default expression and store
        builder_.setInsertPoint(defaultBB);
        currentBlock_ = defaultBB;
        auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
        auto defaultVal = initExpr ? lowerExpression(initExpr) : builder_.createConstUndefined();
        // Force box the default value if parameter type is Any. We use
        // forceBoxValue because the expression might be a function call that
        // gets inlined later, changing its type from Any to a concrete type.
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

        defineVariableAlloca(paramName, allocaVal, paramType);
        return;
    }

    if (useAlloca) {
        // No default value - store into an alloca so reassignment works
        auto allocaVal = builder_.createAlloca(paramType);
        builder_.createStore(paramValue, allocaVal);
        defineVariableAlloca(paramName, allocaVal, paramType);
    } else {
        // Direct value registration (used by methods — params are not reassigned)
        defineVariable(paramName, paramValue);
    }
}

void ASTToHIR::extractDestructuringForParam(HIRFunction* func,
                                            size_t hirParamIndex,
                                            ast::ObjectBindingPattern* objPattern,
                                            ast::ArrayBindingPattern* arrPattern,
                                            ast::Node* defaultInitializer) {
    auto paramValue = std::make_shared<HIRValue>(
        static_cast<uint32_t>(hirParamIndex),
        HIRType::makeAny(),
        func->params[hirParamIndex].first);
    // Apply parameter default value before destructuring per ECMA-262
    // FunctionDeclarationInstantiation step on FormalParameters with
    // Initializer: if the actual argument is undefined, use the default.
    if (auto* defaultExpr = dynamic_cast<ast::Expression*>(defaultInitializer)) {
        auto isUndef = builder_.createIsUndefined(paramValue);
        auto defaultVal = lowerExpression(defaultExpr);
        defaultVal = boxValueIfNeeded(defaultVal);
        paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
    }
    if (objPattern) {
        lowerObjectBindingPattern(objPattern, paramValue);
    } else if (arrPattern) {
        lowerArrayBindingPattern(arrPattern, paramValue);
    }
}

//==============================================================================
// Type Conversion
//==============================================================================

std::shared_ptr<HIRType> ASTToHIR::convertTypeFromString(const std::string& typeStr) {
    if (typeStr.empty()) {
        return HIRType::makeAny();
    }

    // Handle basic TypeScript type names
    if (typeStr == "number") {
        // In TypeScript, 'number' is always IEEE 754 double-precision float
        return HIRType::makeFloat64();
    }
    // "use fast" fixed-width numeric aliases (docs/design/use-fast.md), gated to
    // "use fast" files so non-fast compilation is unchanged. HIR has only
    // Int64/Float64 widths today, so integers -> Int64 and floats -> Float64
    // (exact machine widths are a later refinement). The point is that an
    // f64/i32 struct field gets a proper unboxed numeric slot instead of
    // falling through to makeAny() -> boxed slot (which silently broke stores).
    if (fastCode_) {
        if (typeStr == "i8"  || typeStr == "i16" || typeStr == "i32" || typeStr == "i64" ||
            typeStr == "u8"  || typeStr == "u16" || typeStr == "u32" || typeStr == "u64" ||
            typeStr == "usize" || typeStr == "isize") {
            return HIRType::makeInt64();
        }
        if (typeStr == "f32" || typeStr == "f64") {
            return HIRType::makeFloat64();
        }
    }
    if (typeStr == "string") {
        return HIRType::makeString();
    } else if (typeStr == "boolean") {
        return HIRType::makeBool();
    } else if (typeStr == "void") {
        return HIRType::makeVoid();
    } else if (typeStr == "null") {
        return HIRType::makePtr();
    } else if (typeStr == "undefined") {
        return HIRType::makePtr();
    } else if (typeStr == "any") {
        return HIRType::makeAny();
    } else if (typeStr == "unknown") {
        return HIRType::makeAny();
    } else if (typeStr == "object") {
        return HIRType::makeObject();
    } else if (typeStr == "never") {
        return HIRType::makeVoid();
    } else if (typeStr.find("[]") != std::string::npos) {
        // Array type like "number[]"
        std::string elemType = typeStr.substr(0, typeStr.length() - 2);
        return HIRType::makeArray(convertTypeFromString(elemType));
    } else if (typeStr.find("Array<") == 0) {
        // Array<T> syntax
        size_t start = 6;  // Length of "Array<"
        size_t end = typeStr.rfind('>');
        if (end != std::string::npos && end > start) {
            std::string elemType = typeStr.substr(start, end - start);
            return HIRType::makeArray(convertTypeFromString(elemType));
        }
        return HIRType::makeArray(HIRType::makeAny());
    } else if (typeStr.find("Promise<") == 0) {
        // Promise<T> - treat as ptr for now
        return HIRType::makePtr();
    } else if (typeStr.find("=>") != std::string::npos) {
        // Arrow function type syntax like "() => number" or "(x: number) => number"
        // These are function types, represented as pointers (closures)
        auto funcType = std::make_shared<HIRType>(HIRTypeKind::Function);
        // Parse the return type after "=>"
        size_t arrowPos = typeStr.find("=>");
        if (arrowPos != std::string::npos) {
            std::string retTypeStr = typeStr.substr(arrowPos + 2);
            // Trim leading whitespace
            while (!retTypeStr.empty() && (retTypeStr[0] == ' ' || retTypeStr[0] == '\t')) {
                retTypeStr = retTypeStr.substr(1);
            }
            funcType->returnType = convertTypeFromString(retTypeStr);
        } else {
            funcType->returnType = HIRType::makeAny();
        }
        return funcType;
    }

    // Unknown type - preserve class name for property resolution
    return HIRType::makeClass(typeStr, 0);
}

std::shared_ptr<HIRType> ASTToHIR::convertType(const std::shared_ptr<ts::Type>& type) {
    if (!type) {
        return HIRType::makeAny();
    }

    switch (type->kind) {
        case ts::TypeKind::Void:
            return HIRType::makeVoid();
        case ts::TypeKind::Boolean:
            return HIRType::makeBool();
        case ts::TypeKind::Int:
            return HIRType::makeInt64();
        case ts::TypeKind::Double:
            return HIRType::makeFloat64();
        case ts::TypeKind::String:
            return HIRType::makeString();
        case ts::TypeKind::Any:
        case ts::TypeKind::Unknown:
            return HIRType::makeAny();
        case ts::TypeKind::Null:
        case ts::TypeKind::Undefined:
            return HIRType::makePtr();  // null/undefined are ptr type
        case ts::TypeKind::Array:
            if (auto arrType = std::dynamic_pointer_cast<ts::ArrayType>(type)) {
                return HIRType::makeArray(convertType(arrType->elementType));
            }
            return HIRType::makeArray(HIRType::makeAny());
        case ts::TypeKind::Object:
            return HIRType::makeObject();
        case ts::TypeKind::Class: {
            // Preserve class type information including the class name
            if (auto classType = std::dynamic_pointer_cast<ts::ClassType>(type)) {
                // If the class name comes from a user-imported module (not a real HIR class),
                // use Any instead of Class to prevent extension dispatch from intercepting
                // user-defined classes that happen to share names with built-in types
                // (e.g., eventemitter3's EventEmitter vs the built-in events EventEmitter).
                if (isModuleGlobalVar(classType->name)) {
                    bool isRealHIRClass = false;
                    for (auto& cls : module_->classes) {
                        if (cls->name == classType->name) {
                            isRealHIRClass = true;
                            break;
                        }
                    }
                    if (!isRealHIRClass) {
                        return HIRType::makeAny();
                    }
                }
                return HIRType::makeClass(classType->name, 0);
            }
            return HIRType::makeObject();  // Fallback to generic object
        }
        case ts::TypeKind::BigInt:
            return HIRType::makeObject();  // BigInt is a heap-allocated object
        case ts::TypeKind::Function: {
            // Preserve function type information for closures
            auto funcType = std::dynamic_pointer_cast<ts::FunctionType>(type);
            if (funcType) {
                auto hirFuncType = std::make_shared<HIRType>(HIRTypeKind::Function);
                for (const auto& paramType : funcType->paramTypes) {
                    hirFuncType->paramTypes.push_back(convertType(paramType));
                }
                if (funcType->returnType) {
                    hirFuncType->returnType = convertType(funcType->returnType);
                } else {
                    hirFuncType->returnType = HIRType::makeAny();
                }
                return hirFuncType;
            }
            return HIRType::makePtr();  // Fallback to generic pointer
        }
        default:
            return HIRType::makeAny();
    }
}

// Install a class's computed-name accessors (`get [expr]()` / `set [expr]()`)
// onto the freshly-built prototype (instance accessors) or constructor object
// (static accessors). The key expression is evaluated here and the runtime
// prepends the `__getter_`/`__setter_` prefix and applies the spec method
// descriptor. Shared by the class-declaration deferred install and the
// class-expression inline installs.

}  // namespace ts::hir
