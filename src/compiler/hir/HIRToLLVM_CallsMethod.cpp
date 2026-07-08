#include "HIRToLLVM_Internal.h"

namespace ts::hir {


void HIRToLLVM::lowerCallMethod(HIRInstruction* inst) {
    // operands[0] = object, operands[1] = methodName, operands[2..] = args
    llvm::Value* obj = getOperandValue(inst->operands[0]);
    std::string methodName = getOperandString(inst->operands[1]);

    // Handle console.log / console.error / console.warn / console.info / console.debug via registry
    // NOTE: The BuiltinResolutionPass already resolves direct console.X() calls to ts_console_X.
    // This path handles edge cases (e.g., const c = console; c.log(...)).
    // Guard: skip if the receiver is a known Object type (user-defined object literal, not console).
    bool receiverIsObject = false;
    bool receiverIsAny = false;
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*valPtr && (*valPtr)->type) {
            if ((*valPtr)->type->kind == HIRTypeKind::Object) {
                receiverIsObject = true;
            } else if ((*valPtr)->type->kind == HIRTypeKind::Any) {
                receiverIsAny = true;
            }
        }
    }
    if (!receiverIsObject &&
        (methodName == "log" || methodName == "error" || methodName == "warn" ||
         methodName == "info" || methodName == "debug")) {

        // Determine the base runtime function name
        std::string baseFuncName = (methodName == "error" || methodName == "warn")
            ? "ts_console_error" : "ts_console_log";

        // Look up the lowering spec from the registry
        auto& registry = ::hir::LoweringRegistry::instance();
        const ::hir::LoweringSpec* spec = registry.lookup(baseFuncName);

        if (spec && spec->variadicHandling == ::hir::VariadicHandling::TypeDispatch) {
            // Use registry-driven type dispatch
            // For console methods, operands[2..] are the arguments (skip object and methodName)
            for (size_t i = 2; i < inst->operands.size(); ++i) {
                llvm::Value* arg = getOperandValue(inst->operands[i]);

                // Get type suffix based on argument type
                std::string suffix = getTypeSuffix(arg, *spec);
                std::string funcName = baseFuncName + suffix;

                // Get the LLVM type for this argument
                llvm::Type* paramType = arg->getType();

                // Emit the call
                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder_->getVoidTy(), { paramType }, false);
                llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
                builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
        } else {
            // Fallback for non-registered functions (shouldn't happen with proper registration)
            for (size_t i = 2; i < inst->operands.size(); ++i) {
                llvm::Value* arg = getOperandValue(inst->operands[i]);

                std::string funcName = baseFuncName + "_value";
                llvm::Type* paramType = getGCPtrTy();

                // Convert to pointer type if needed
                if (!arg->getType()->isPointerTy()) {
                    arg = convertArg(arg, ::hir::ArgConversion::Box);
                }

                llvm::FunctionType* ft = llvm::FunctionType::get(
                    builder_->getVoidTy(), { paramType }, false);
                llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
                builder_->CreateCall(ft, fn.getCallee(), { arg });
            }
        }

        // console.log returns undefined
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(getGCPtrTy()));
        }
        return;
    }

    // Check if we can determine the object type from the HIRValue
    std::string className;
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*valPtr && (*valPtr)->type) {
            if ((*valPtr)->type->kind == HIRTypeKind::Class) {
                className = (*valPtr)->type->className;
            } else if ((*valPtr)->type->kind == HIRTypeKind::Map) {
                className = "Map";
            } else if ((*valPtr)->type->kind == HIRTypeKind::Set) {
                className = "Set";
            }
        }
    }

    // Try handler registry first (for Map/Set method calls, etc.)
    if (auto* result = HandlerRegistry::instance().tryLowerMethod(methodName, className, inst, *this)) {
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // Generator, AsyncGenerator, and RegExp methods handled by GeneratorHandler and RegExpHandler
    // via HandlerRegistry::tryLowerMethod() above

    // Try to look up user-defined class method
    if (!className.empty()) {
        std::string funcName = className + "_" + methodName;
        llvm::Function* fn = module_->getFunction(funcName);
        if (fn) {
            // Found the method function - call it with 'this' and arguments
            llvm::FunctionType* fnType = fn->getFunctionType();
            std::vector<llvm::Value*> args;
            args.push_back(obj);  // 'this' pointer (param 0)

            // Look up HIR parameter types to pass to coerceArgToType
            // so it knows whether to box string arguments or not
            std::vector<std::shared_ptr<HIRType>>* hirParamTypes = nullptr;
            auto it = userFunctionParams_.find(funcName);
            if (it != userFunctionParams_.end()) {
                hirParamTypes = &it->second;
            }

            for (size_t i = 2; i < inst->operands.size(); ++i) {
                llvm::Value* arg = getOperandValue(inst->operands[i]);
                size_t paramIdx = i - 2 + 1;  // +1 for 'this' param
                if (paramIdx < fnType->getNumParams()) {
                    llvm::Type* expectedType = fnType->getParamType(paramIdx);
                    std::shared_ptr<HIRType> calleeParamType;
                    if (hirParamTypes && paramIdx < hirParamTypes->size()) {
                        calleeParamType = (*hirParamTypes)[paramIdx];
                    }
                    arg = coerceArgToType(arg, expectedType, inst->operands[i], calleeParamType);
                }
                args.push_back(arg);
            }
            // Pad missing args with undefined to match callee arity. LLVM
            // verifier rejects mismatched call arity; this is the same fix
            // applied to direct-call sites elsewhere.
            while (args.size() < fnType->getNumParams()) {
                llvm::Type* expectedType = fnType->getParamType(args.size());
                if (expectedType->isPointerTy()) {
                    // NaN-box undefined sentinel (0x0A)
                    args.push_back(builder_->CreateIntToPtr(
                        llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0A),
                        getGCPtrTy()));
                } else if (expectedType->isDoubleTy()) {
                    args.push_back(llvm::ConstantFP::get(builder_->getDoubleTy(),
                                                         std::numeric_limits<double>::quiet_NaN()));
                } else {
                    args.push_back(llvm::Constant::getNullValue(expectedType));
                }
            }
            // Truncate extra args (caller passed more than callee declares)
            if (args.size() > fnType->getNumParams()) {
                args.resize(fnType->getNumParams());
            }

            // Set ts_last_call_argc (JS arg count, receiver excluded) so the
            // callee's `arguments` object reports the right length/elements.
            // This direct method-call path bypasses ts_call_with_this_N (which
            // would otherwise set it). operands[0]=obj, [1]=method name,
            // [2..]=JS args.
            {
                int64_t jsArgc = (int64_t)inst->operands.size() - 2;
                if (jsArgc < 0) jsArgc = 0;
                auto setArgcFt = llvm::FunctionType::get(
                    builder_->getVoidTy(), { builder_->getInt64Ty() }, false);
                auto setArgcFn = module_->getOrInsertFunction("ts_set_last_call_argc", setArgcFt);
                builder_->CreateCall(setArgcFt, setArgcFn.getCallee(),
                    { llvm::ConstantInt::get(builder_->getInt64Ty(), jsArgc) });
            }

            llvm::Value* result = builder_->CreateCall(fn, args);
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }

        // Array variadic methods (splice/push/concat/unshift) go through
        // the explicit handlers below (line ~6153+), NOT the spec-driven
        // lookup — they need the loop-of-calls / items-packing pattern
        // which the generic specArgIdx truncation cannot express.
        static const std::unordered_set<std::string> arrayVariadicMethods = {
            "splice", "push", "concat", "unshift"
        };
        bool isArrayVariadic = (className == "Array" &&
                                 arrayVariadicMethods.count(methodName));

        // Try extension type method from LoweringRegistry (e.g., ts_Cipher_update)
        // Skip types that use property dispatch (no standalone C functions):
        // fs extension's Dir, Dirent, Stats, FSWatcher, FsPromises types
        // implement methods as TsMap properties, not extern "C" functions.
        {
            static const std::unordered_set<std::string> propertyDispatchTypes = {
                "Stats", "Dirent", "Dir", "FSWatcher", "FsPromises"
            };
            std::string hirName = "ts_" + className + "_" + methodName;
            auto& registry = ::hir::LoweringRegistry::instance();
            const auto* spec = (!propertyDispatchTypes.count(className) && !isArrayVariadic)
                ? registry.lookup(hirName)
                : nullptr;
            if (spec) {
                // Handle PackArray variadic methods (e.g., EventEmitter.emit)
                if (spec->variadicHandling == ::hir::VariadicHandling::PackArray) {
                    // restParamIndex is in spec arg space (0=self, 1=first method arg, ...)
                    // For CallMethod: operands[0]=obj, [1]=methodName, [2..]=args
                    // Spec arg 0 = self (obj), spec arg N maps to operands[N+1]
                    size_t restSpecIdx = spec->restParamIndex + 1; // +1 because self is arg 0
                    // Fixed operand index where rest starts: self + restSpecIdx method args + 2 (obj, methodName)
                    size_t restOperandIdx = restSpecIdx + 1; // operands index where rest args begin

                    // Create array for rest arguments
                    auto createFt = llvm::FunctionType::get(getGCPtrTy(), {}, false);
                    auto createFn = module_->getOrInsertFunction("ts_array_create", createFt);
                    llvm::Value* restArray = rawToGCPtr(builder_->CreateCall(createFt, createFn.getCallee(), {}));

                    // Push each rest argument into the array
                    auto pushFt = llvm::FunctionType::get(builder_->getInt64Ty(),
                        { getGCPtrTy(), getGCPtrTy() }, false);
                    auto pushFn = module_->getOrInsertFunction("ts_array_push", pushFt);

                    for (size_t i = restOperandIdx; i < inst->operands.size(); ++i) {
                        llvm::Value* arg = getOperandValue(inst->operands[i]);
                        arg = convertArg(arg, ::hir::ArgConversion::Box);
                        builder_->CreateCall(pushFt, pushFn.getCallee(), { gcPtrToRaw(restArray), arg });
                    }

                    // Build fixed args: self + fixed method args
                    std::vector<llvm::Value*> llvmArgs;
                    size_t specArgIdx = 0;

                    // Self arg
                    if (specArgIdx < spec->argConversions.size()) {
                        llvm::Value* selfArg = convertArg(obj, spec->argConversions[specArgIdx]);
                        llvmArgs.push_back(selfArg);
                        specArgIdx++;
                    }

                    // Fixed method args (before rest)
                    for (size_t i = 2; i < restOperandIdx && i < inst->operands.size() && specArgIdx < spec->argConversions.size(); ++i, ++specArgIdx) {
                        llvm::Value* arg = getOperandValue(inst->operands[i]);
                        arg = convertArg(arg, spec->argConversions[specArgIdx]);
                        llvmArgs.push_back(arg);
                    }

                    // Append the packed rest array
                    llvmArgs.push_back(gcPtrToRaw(restArray));

                    // Build LLVM function type
                    llvm::Type* retTy = spec->returnType
                        ? spec->returnType(context_)
                        : builder_->getVoidTy();

                    std::vector<llvm::Type*> argTys;
                    for (const auto& argType : spec->argTypes) {
                        argTys.push_back(argType(context_));
                    }
                    // Add rest array type if not in spec
                    if (argTys.size() < llvmArgs.size()) {
                        argTys.push_back(getGCPtrTy());
                    }

                    auto* ft = llvm::FunctionType::get(retTy, argTys, false);
                    auto fn = module_->getOrInsertFunction(spec->runtimeFuncName, ft);

                    llvm::Value* result;
                    if (retTy->isVoidTy()) {
                        builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
                        result = llvm::ConstantPointerNull::get(getGCPtrTy());
                    } else {
                        result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
                    }

                    result = handleReturn(result, spec->returnHandling);

                    if (inst->result) {
                        setValue(inst->result, result);
                    }
                    return;
                }

                // Build LLVM function type from spec
                llvm::Type* retTy = spec->returnType
                    ? spec->returnType(context_)
                    : builder_->getVoidTy();

                std::vector<llvm::Type*> argTys;
                for (const auto& argType : spec->argTypes) {
                    argTys.push_back(argType(context_));
                }

                auto* ft = llvm::FunctionType::get(retTy, argTys, spec->isVariadic);
                auto fn = module_->getOrInsertFunction(spec->runtimeFuncName, ft);

                // Build arguments: spec includes ptrArg for self + remaining args
                std::vector<llvm::Value*> llvmArgs;
                size_t specArgIdx = 0;

                // First arg in spec is self (ptrArg for instance methods)
                if (specArgIdx < spec->argConversions.size()) {
                    llvm::Value* selfArg = convertArg(obj, spec->argConversions[specArgIdx]);
                    if (specArgIdx < argTys.size() && selfArg->getType() != argTys[specArgIdx]) {
                        selfArg = coerceArgToType(selfArg, argTys[specArgIdx], inst->operands[0]);
                    }
                    llvmArgs.push_back(selfArg);
                    specArgIdx++;
                }

                // Remaining args from operands[2..]
                for (size_t i = 2; i < inst->operands.size() && specArgIdx < spec->argConversions.size(); ++i, ++specArgIdx) {
                    llvm::Value* arg = getOperandValue(inst->operands[i]);
                    arg = convertArg(arg, spec->argConversions[specArgIdx]);
                    if (specArgIdx < argTys.size() && arg->getType() != argTys[specArgIdx]) {
                        arg = coerceArgToType(arg, argTys[specArgIdx], inst->operands[i]);
                    }
                    llvmArgs.push_back(arg);
                }

                // Pad missing optional arguments
                while (llvmArgs.size() < argTys.size()) {
                    llvm::Type* expectedType = argTys[llvmArgs.size()];
                    if (expectedType->isPointerTy())
                        llvmArgs.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(expectedType)));
                    else if (expectedType->isIntegerTy())
                        llvmArgs.push_back(llvm::ConstantInt::get(expectedType, 0));
                    else if (expectedType->isDoubleTy())
                        llvmArgs.push_back(llvm::ConstantFP::get(expectedType, 0.0));
                    else
                        llvmArgs.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
                }

                llvm::Value* result;
                if (retTy->isVoidTy()) {
                    builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
                    result = llvm::ConstantPointerNull::get(getGCPtrTy());
                } else {
                    result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
                }

                result = handleReturn(result, spec->returnHandling);

                if (inst->result) {
                    setValue(inst->result, result);
                }
                return;
            }
        }
    }

    // For receivers that are NOT statically known to be Arrays, skip the
    // hardcoded array-method dispatches. The runtime fallback paths (which
    // detect the brand) have signatures that lose extra args — e.g.
    // ts_array_join(arr, sep) can't carry a third arg, so `_.join(arr, sep)`
    // would silently drop `sep`. Better: emit a generic property-lookup +
    // call_indirect when the receiver type is Any.
    bool receiverIsArrayLike = false;
    if (auto* valPtr = std::get_if<std::shared_ptr<HIRValue>>(&inst->operands[0])) {
        if (*valPtr && (*valPtr)->type) {
            HIRTypeKind k = (*valPtr)->type->kind;
            if (k == HIRTypeKind::Array) {
                receiverIsArrayLike = true;
            }
        }
    }

    // Handle common array methods on Any-typed values
    // This handles cases where MethodResolutionPass couldn't resolve due to Any type
    if (receiverIsArrayLike && methodName == "join") {
        // ts_array_join(void* arr, void* separator) -> TsString*
        // Box primitive separators so the call type matches the runtime sig.
        llvm::Value* separator = llvm::ConstantPointerNull::get(getGCPtrTy());
        if (inst->operands.size() > 2) {
            separator = getOperandValue(inst->operands[2]);
            if (separator->getType()->isDoubleTy()) {
                auto ft0 = llvm::FunctionType::get(
                    getGCPtrTy(), { builder_->getDoubleTy() }, false);
                auto fn0 = module_->getOrInsertFunction("ts_value_make_double", ft0);
                separator = builder_->CreateCall(ft0, fn0.getCallee(), { separator });
            } else if (separator->getType()->isIntegerTy(64)) {
                separator = builder_->CreateCall(getTsValueMakeInt(), { separator });
            } else if (separator->getType()->isIntegerTy(1)) {
                llvm::Value* w = builder_->CreateZExt(separator, builder_->getInt32Ty());
                separator = builder_->CreateCall(getTsValueMakeBool(), { w });
            } else if (separator->getType()->isIntegerTy(32)) {
                separator = builder_->CreateCall(getTsValueMakeBool(), { separator });
            }
        }
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_join", ft);
        llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), { obj, separator });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (methodName == "push" && !receiverIsObject && !receiverIsAny) {
        // ts_array_push(void* arr, void* value) -> int64_t (new length)
        // Variadic: arr.push(a, b, c) emits N sequential calls and returns
        // the length from the final call.
        // Guard: a receiver of statically-known Object type (object literal
        // with its own `push` method, e.g. a lodash chain wrapper) must NOT
        // be force-dispatched to the native array push — it threads the i64
        // length result as the next call's receiver, producing invalid IR
        // (`ts_array_push(i64, ptr)`). Such receivers fall through to the
        // dynamic property-dispatch path below.
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_push", ft);
        llvm::Value* result = nullptr;
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* val = getOperandValue(inst->operands[i]);

            // Box the value if needed
            if (!val->getType()->isPointerTy()) {
                if (val->getType()->isIntegerTy(1)) {
                    auto boxFn = getTsValueMakeBool();
                    llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
                    val = builder_->CreateCall(boxFn, {extended});
                } else if (val->getType()->isIntegerTy()) {
                    llvm::Value* asI64 = builder_->CreateSExtOrTrunc(val, builder_->getInt64Ty());
                    auto boxFn = getTsValueMakeInt();
                    val = builder_->CreateCall(boxFn, {asI64});
                } else if (val->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    val = builder_->CreateCall(boxFn, {val});
                } else if (val->getType()->isFloatTy()) {
                    llvm::Value* asDouble = builder_->CreateFPExt(val, builder_->getDoubleTy());
                    auto boxFn = getTsValueMakeDouble();
                    val = builder_->CreateCall(boxFn, {asDouble});
                }
            }

            result = builder_->CreateCall(ft, fn.getCallee(), { obj, val });
        }
        // Zero-arg push returns the CURRENT length (S15.4.4.7 `x.push()`),
        // not an unset/garbage result.
        if (inst->result && !result) {
            llvm::FunctionType* lft = llvm::FunctionType::get(
                builder_->getInt64Ty(), { getGCPtrTy() }, false);
            llvm::FunctionCallee lfn =
                module_->getOrInsertFunction("ts_array_length", lft);
            result = builder_->CreateCall(lft, lfn.getCallee(), { obj });
        }
        if (inst->result && result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (methodName == "unshift" && !receiverIsObject && !receiverIsAny) {
        // ts_array_unshift(void* arr, void* value) -> int64_t (new length)
        // Guard (mirrors push above): a statically-known Object/any receiver
        // with its own `unshift` method (e.g. a lodash lazy-wrapper, which
        // assigns Array method names onto its prototype and returns `this`
        // for chaining) must NOT be force-dispatched to native array unshift —
        // that threads the i64 length as the receiver and discards the
        // wrapper. Such receivers fall through to dynamic property dispatch.
        // Per ES spec, arr.unshift(a, b, c) prepends so that a is at index 0.
        // Each single-arg unshift prepends one element at index 0. To match
        // spec ordering, iterate the args in REVERSE so that the first arg
        // ends up at index 0 (the LAST call wins index 0).
        llvm::FunctionType* ft = llvm::FunctionType::get(
            builder_->getInt64Ty(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_unshift", ft);
        llvm::Value* result = nullptr;
        for (size_t i = inst->operands.size(); i > 2; --i) {
            llvm::Value* val = getOperandValue(inst->operands[i - 1]);
            if (!val->getType()->isPointerTy()) {
                if (val->getType()->isIntegerTy(1)) {
                    auto boxFn = getTsValueMakeBool();
                    llvm::Value* extended = builder_->CreateZExt(val, builder_->getInt32Ty());
                    val = builder_->CreateCall(boxFn, {extended});
                } else if (val->getType()->isIntegerTy()) {
                    llvm::Value* asI64 = builder_->CreateSExtOrTrunc(val, builder_->getInt64Ty());
                    auto boxFn = getTsValueMakeInt();
                    val = builder_->CreateCall(boxFn, {asI64});
                } else if (val->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    val = builder_->CreateCall(boxFn, {val});
                } else if (val->getType()->isFloatTy()) {
                    llvm::Value* asDouble = builder_->CreateFPExt(val, builder_->getDoubleTy());
                    auto boxFn = getTsValueMakeDouble();
                    val = builder_->CreateCall(boxFn, {asDouble});
                }
            }
            result = builder_->CreateCall(ft, fn.getCallee(), { obj, val });
        }
        // Zero-arg unshift returns the CURRENT length (S15.4.4.13
        // `x.unshift()`), not an unset result.
        if (inst->result && !result) {
            llvm::FunctionType* lft = llvm::FunctionType::get(
                builder_->getInt64Ty(), { getGCPtrTy() }, false);
            llvm::FunctionCallee lfn =
                module_->getOrInsertFunction("ts_array_length", lft);
            result = builder_->CreateCall(lft, lfn.getCallee(), { obj });
        }
        if (inst->result && result) {
            setValue(inst->result, result);
        }
        return;
    }

    if (receiverIsArrayLike && methodName == "concat") {
        // ts_array_concat(void* arr, void* other) returns a new array.
        // Variadic: arr.concat(a, b, c) iterates, chaining the result
        // through each call. Each arg may itself be an array (spreadable)
        // or a single value — the runtime distinguishes.
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { getGCPtrTy(), getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_array_concat", ft);
        llvm::Value* acc = obj;
        if (inst->operands.size() <= 2) {
            // Zero-arg concat: spec still builds a NEW array through
            // ArraySpeciesCreate — returning the receiver skips the copy
            // and the species/constructor validation TypeErrors.
            llvm::FunctionType* ft0 = llvm::FunctionType::get(
                getGCPtrTy(), { getGCPtrTy() }, false);
            llvm::FunctionCallee fn0 =
                module_->getOrInsertFunction("ts_array_concat_none", ft0);
            acc = builder_->CreateCall(ft0, fn0.getCallee(), { obj });
        }
        for (size_t i = 2; i < inst->operands.size(); ++i) {
            llvm::Value* other = getOperandValue(inst->operands[i]);
            if (!other->getType()->isPointerTy()) {
                if (other->getType()->isIntegerTy(1)) {
                    auto boxFn = getTsValueMakeBool();
                    llvm::Value* extended = builder_->CreateZExt(other, builder_->getInt32Ty());
                    other = builder_->CreateCall(boxFn, {extended});
                } else if (other->getType()->isIntegerTy()) {
                    // Handle i8, i16, i32, i64 by extending to i64 then boxing.
                    llvm::Value* asI64 = builder_->CreateSExtOrTrunc(other, builder_->getInt64Ty());
                    auto boxFn = getTsValueMakeInt();
                    other = builder_->CreateCall(boxFn, {asI64});
                } else if (other->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    other = builder_->CreateCall(boxFn, {other});
                } else if (other->getType()->isFloatTy()) {
                    llvm::Value* asDouble = builder_->CreateFPExt(other, builder_->getDoubleTy());
                    auto boxFn = getTsValueMakeDouble();
                    other = builder_->CreateCall(boxFn, {asDouble});
                }
            }
            acc = builder_->CreateCall(ft, fn.getCallee(), { acc, other });
        }
        if (inst->result) {
            setValue(inst->result, acc);
        }
        return;
    }

    if ((methodName == "splice" || methodName == "toSpliced") && !receiverIsObject && !receiverIsAny) {
        // splice(start, deleteCount, ...items)
        // Guard (mirrors push/unshift): a statically-known Object/any receiver
        // with its own `splice` method (e.g. a lodash lazy-wrapper) must fall
        // through to dynamic property dispatch rather than native array splice.
        // ts_array_splice(arr, start, deleteCount, items) expects items
        // as a TsArray*. Pack operands[4..] into a temp TsArray, then call.
        auto getBoxed = [&](size_t opIdx) -> llvm::Value* {
            llvm::Value* v = getOperandValue(inst->operands[opIdx]);
            if (!v->getType()->isPointerTy()) {
                if (v->getType()->isIntegerTy(64)) {
                    auto boxFn = getTsValueMakeInt();
                    v = builder_->CreateCall(boxFn, {v});
                } else if (v->getType()->isDoubleTy()) {
                    auto boxFn = getTsValueMakeDouble();
                    v = builder_->CreateCall(boxFn, {v});
                } else if (v->getType()->isIntegerTy(1)) {
                    auto boxFn = getTsValueMakeBool();
                    llvm::Value* extended = builder_->CreateZExt(v, builder_->getInt32Ty());
                    v = builder_->CreateCall(boxFn, {extended});
                }
            }
            return v;
        };
        auto toI64 = [&](llvm::Value* v) -> llvm::Value* {
            if (v->getType()->isIntegerTy(64)) return v;
            if (v->getType()->isDoubleTy()) return builder_->CreateFPToSI(v, builder_->getInt64Ty());
            if (v->getType()->isPointerTy()) {
                // Unbox via ts_value_get_int
                auto ft = llvm::FunctionType::get(
                    builder_->getInt64Ty(), { getGCPtrTy() }, false);
                auto fn = module_->getOrInsertFunction("ts_value_get_int", ft);
                return builder_->CreateCall(ft, fn.getCallee(), { v });
            }
            return builder_->CreateSExtOrTrunc(v, builder_->getInt64Ty());
        };
        llvm::Value* startV = inst->operands.size() > 2
            ? toI64(getOperandValue(inst->operands[2]))
            : llvm::ConstantInt::get(builder_->getInt64Ty(), 0);
        llvm::Value* delCnt = inst->operands.size() > 3
            ? toI64(getOperandValue(inst->operands[3]))
            : llvm::ConstantInt::get(builder_->getInt64Ty(), 0x7fffffffffffffffLL);

        // Pack items (operands[4..]) into a temp TsArray.
        llvm::Value* itemsArr = llvm::ConstantPointerNull::get(getGCPtrTy());
        if (inst->operands.size() > 4) {
            // arr = ts_array_create()
            auto createFt = llvm::FunctionType::get(getGCPtrTy(), {}, false);
            auto createFn = module_->getOrInsertFunction("ts_array_create", createFt);
            itemsArr = builder_->CreateCall(createFt, createFn.getCallee(), {});
            auto pushFt = llvm::FunctionType::get(
                builder_->getInt64Ty(), { getGCPtrTy(), getGCPtrTy() }, false);
            auto pushFn = module_->getOrInsertFunction("ts_array_push", pushFt);
            for (size_t i = 4; i < inst->operands.size(); ++i) {
                llvm::Value* item = getBoxed(i);
                builder_->CreateCall(pushFt, pushFn.getCallee(), { itemsArr, item });
            }
        }

        if (methodName == "toSpliced") {
            // ts_array_toSpliced(arr, start, deleteCount, items, itemCount).
            // Previously toSpliced had NO packer: the raw first item landed
            // in the `items` slot (a nanbox as a TsArray* — CDB'd) and the
            // second item's value became itemCount. Untyped inserts were
            // silently dropped by the runtime guard.
            int64_t itemCount = inst->operands.size() > 4
                ? (int64_t)(inst->operands.size() - 4) : 0;
            auto tsFt = llvm::FunctionType::get(
                getGCPtrTy(),
                { getGCPtrTy(), builder_->getInt64Ty(),
                  builder_->getInt64Ty(), getGCPtrTy(),
                  builder_->getInt64Ty() },
                false);
            auto tsFn = module_->getOrInsertFunction("ts_array_toSpliced", tsFt);
            llvm::Value* result = builder_->CreateCall(tsFt, tsFn.getCallee(),
                { obj, startV, delCnt, itemsArr,
                  llvm::ConstantInt::get(builder_->getInt64Ty(), itemCount) });
            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
        // ts_array_splice(arr, start, deleteCount, items) -> ptr (deleted elements)
        auto spliceFt = llvm::FunctionType::get(
            getGCPtrTy(),
            { getGCPtrTy(), builder_->getInt64Ty(),
              builder_->getInt64Ty(), getGCPtrTy() },
            false);
        auto spliceFn = module_->getOrInsertFunction("ts_array_splice", spliceFt);
        llvm::Value* result = builder_->CreateCall(spliceFt, spliceFn.getCallee(),
            { obj, startV, delCnt, itemsArr });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // RegExp methods (exec, test) handled by RegExpHandler via HandlerRegistry above
    // Map/Set methods handled by MapSetHandler via HandlerRegistry above

    // Check if the method name matches a registered nested object method (e.g., util.types.isGeneratorObject)
    // These methods have registered lowering specs but are called via dynamic dispatch on Any-typed objects.
    // Dynamic dispatch would use ts_call_with_this_N which passes (context, args...) but the C functions
    // only expect (args...) without a context/this parameter.
    {
        auto& registry = ::hir::LoweringRegistry::instance();
        const auto* spec = registry.lookupByMethodName(methodName);
        if (spec) {
            // Build LLVM function type from the lowering spec
            llvm::Type* retTy = spec->returnType
                ? spec->returnType(context_)
                : builder_->getVoidTy();

            std::vector<llvm::Type*> argTys;
            for (const auto& argType : spec->argTypes) {
                argTys.push_back(argType(context_));
            }

            auto* ft = llvm::FunctionType::get(retTy, argTys, spec->isVariadic);
            auto fn = module_->getOrInsertFunction(spec->runtimeFuncName, ft);

            // Convert arguments - for call_method, args start at operands[2]
            std::vector<llvm::Value*> llvmArgs;
            for (size_t i = 2; i < inst->operands.size() && (i - 2) < spec->argConversions.size(); ++i) {
                llvm::Value* arg = getOperandValue(inst->operands[i]);

                // Apply conversion from the spec
                if (spec->argConversions[i - 2] == ::hir::ArgConversion::Box && arg->getType()->isPointerTy()) {
                    auto* hirVal = std::get_if<std::shared_ptr<ts::hir::HIRValue>>(&inst->operands[i]);
                    if (hirVal && *hirVal && (*hirVal)->type && (*hirVal)->type->kind == ts::hir::HIRTypeKind::String) {
                        auto boxFt = llvm::FunctionType::get(getGCPtrTy(), { getGCPtrTy() }, false);
                        auto boxFn = module_->getOrInsertFunction("ts_value_make_string", boxFt);
                        arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { arg });
                    } else {
                        arg = convertArg(arg, spec->argConversions[i - 2]);
                    }
                } else {
                    arg = convertArg(arg, spec->argConversions[i - 2]);
                }

                // Coerce to expected type
                size_t argIdx = i - 2;
                if (argIdx < argTys.size() && arg->getType() != argTys[argIdx]) {
                    arg = coerceArgToType(arg, argTys[argIdx], inst->operands[i]);
                }

                llvmArgs.push_back(arg);
            }

            // Pad missing optional arguments
            while (llvmArgs.size() < argTys.size()) {
                llvm::Type* expectedType = argTys[llvmArgs.size()];
                if (expectedType->isPointerTy())
                    llvmArgs.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(expectedType)));
                else if (expectedType->isIntegerTy())
                    llvmArgs.push_back(llvm::ConstantInt::get(expectedType, 0));
                else if (expectedType->isDoubleTy())
                    llvmArgs.push_back(llvm::ConstantFP::get(expectedType, 0.0));
                else
                    llvmArgs.push_back(llvm::ConstantPointerNull::get(getGCPtrTy()));
            }

            // Call the function
            llvm::Value* result;
            if (retTy->isVoidTy()) {
                builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
                result = llvm::ConstantPointerNull::get(getGCPtrTy());
            } else {
                result = builder_->CreateCall(ft, fn.getCallee(), llvmArgs);
            }

            // Handle return value
            result = handleReturn(result, spec->returnHandling);

            if (inst->result) {
                setValue(inst->result, result);
            }
            return;
        }
    }

    // Dynamic method dispatch: call function stored as object property
    // This handles cases like task.fn() where fn is a function property
    // Uses ts_call_with_this_N to properly bind 'this' for methods like hasOwnProperty
    {
        // Get the function property from the object
        llvm::FunctionType* getFt = llvm::FunctionType::get(
            getGCPtrTy(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        llvm::FunctionCallee getFn = module_->getOrInsertFunction("ts_object_get_property", getFt);

        // Create method name as global string
        llvm::Constant* methodNameStr = builder_->CreateGlobalStringPtr(methodName, "method_name");

        // Box primitive types before property lookup (e.g., x.toString() where x is a number)
        llvm::Value* boxedObj = obj;
        if (obj->getType()->isDoubleTy()) {
            // Box double to TsValue*
            auto boxFn = getTsValueMakeDouble();
            boxedObj = builder_->CreateCall(boxFn, { obj }, "box_double_for_method");
        } else if (obj->getType()->isIntegerTy(64)) {
            // Box i64 to TsValue*
            auto boxFn = getTsValueMakeInt();
            boxedObj = builder_->CreateCall(boxFn, { obj }, "box_int_for_method");
        } else if (obj->getType()->isIntegerTy(1)) {
            // Box bool to TsValue*. ts_value_make_bool's canonical signature
            // is ptr(i32), so widen i1 → i32 before the call to avoid an
            // LLVM verifier error.
            auto boxFn = getTsValueMakeBool();
            llvm::Value* widened = builder_->CreateZExt(obj, builder_->getInt32Ty(), "bool_widen_for_method");
            boxedObj = builder_->CreateCall(boxFn, { widened }, "box_bool_for_method");
        } else if (!obj->getType()->isPointerTy()) {
            // Other non-pointer types: cast to ptr (shouldn't normally happen)
            boxedObj = builder_->CreateIntToPtr(obj, getGCPtrTy(), "cast_to_ptr_for_method");
        }

        // Get the function property
        llvm::Value* funcVal = builder_->CreateCall(getFt, getFn.getCallee(), { boxedObj, methodNameStr });

        // Box obj as thisArg for ts_call_with_this_N
        // For primitives, boxedObj is already the correct boxed TsValue*
        // For objects, we need to wrap with ts_value_make_object
        llvm::Value* thisArg;
        if (obj->getType()->isDoubleTy() || obj->getType()->isIntegerTy(64) || obj->getType()->isIntegerTy(1)) {
            // Primitive was already boxed above
            thisArg = boxedObj;
        } else {
            // Object: wrap with ts_value_make_object
            llvm::Value* objPtr = obj;
            if (!obj->getType()->isPointerTy()) {
                objPtr = builder_->CreateIntToPtr(obj, getGCPtrTy());
            }
            objPtr = gcPtrToRaw(objPtr);  // Strip addrspace(1) for runtime call
            auto boxObjFn = getTsValueMakeObject();
            thisArg = builder_->CreateCall(boxObjFn, { objPtr });
        }

        // Use helper to emit the dynamic call with boxed arguments
        // Arguments start at operands[2] (after obj and methodName)
        llvm::Value* result = emitDynamicMethodCall(funcVal, thisArg, inst, 2);

        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }
}

void HIRToLLVM::lowerCallVirtual(HIRInstruction* inst) {
    // CallVirtual: %r = call_virtual %obj, <vtable_idx>, (%args...)
    // Used for virtual method dispatch through an object's vtable
    //
    // TsObject layout (with C++ virtual inheritance):
    //   offset 0: C++ vtable pointer (implicit from virtual methods)
    //   offset 8: TsObject::vtable (explicit vtable for custom dispatch)
    //   offset 16: TsObject::magic
    //
    // The explicit vtable is an array of function pointers:
    //   vtable[0] = parent vtable pointer (for inheritance)
    //   vtable[1+] = method function pointers

    llvm::Value* obj = getOperandValue(inst->operands[0]);
    int64_t vtableIdx = getOperandInt(inst->operands[1]);

    // Load the explicit vtable pointer from offset 8 (TsObject::vtable)
    // GEP with i8 type to get byte offset
    llvm::Value* vtablePtrAddr = builder_->CreateGEP(
        builder_->getInt8Ty(),
        obj,
        llvm::ConstantInt::get(builder_->getInt64Ty(), 8),
        "vtable_addr"
    );
    llvm::Value* vtable = builder_->CreateLoad(getGCPtrTy(), vtablePtrAddr, "vtable");

    // Load the function pointer from vtable[vtableIdx]
    // vtable is void** so each entry is 8 bytes
    llvm::Value* funcPtrAddr = builder_->CreateGEP(
        getGCPtrTy(),
        vtable,
        llvm::ConstantInt::get(builder_->getInt64Ty(), vtableIdx),
        "func_ptr_addr"
    );
    llvm::Value* funcPtr = builder_->CreateLoad(getGCPtrTy(), funcPtrAddr, "func_ptr");

    // Build argument list: object as 'this', then remaining arguments
    std::vector<llvm::Value*> args;
    args.push_back(obj);  // 'this' pointer
    for (size_t i = 2; i < inst->operands.size(); ++i) {
        args.push_back(getOperandValue(inst->operands[i]));
    }

    // Build function type: ptr (ptr, ptr, ...) - all pointers
    std::vector<llvm::Type*> paramTypes(args.size(), getGCPtrTy());
    llvm::FunctionType* ft = llvm::FunctionType::get(getGCPtrTy(), paramTypes, false);

    // Call the virtual function
    llvm::Value* result = builder_->CreateCall(ft, funcPtr, args, "vcall_result");

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerCallIndirect(HIRInstruction* inst) {
    // CallIndirect is used to call closures or function pointers dynamically
    // We use ts_call_N runtime functions which handle different function types:
    // - TsClosure: passes closure as context
    // - TsFunction: handles compiled functions with various signatures
    //
    // Operand 0: closure/function pointer (boxed TsValue*)
    // Operand 1+: regular arguments (boxed TsValue*)

    llvm::Value* callablePtr = getOperandValue(inst->operands[0]);
    // The ts_call_N runtime functions expect a TsValue* (ptr) for the
    // callee. If the callable arrived here as a primitive (i64/double/i1)
    // — which happens when a property-access result was inline-decoded
    // because its HIR type was Int64 by upstream type inference (e.g.
    // `it.next` whose result type ended up Int64), the verifier rejects
    // `call ptr @ts_call_N(i64, ...)`. Box the callee so the runtime
    // sees a TsValue* and can throw a clean "not callable" TypeError.
    callablePtr = boxPrimitiveToPtr(callablePtr);

    // Gather regular arguments - ensure they're boxed for the ts_call_N interface
    std::vector<llvm::Value*> regularArgs;
    for (size_t i = 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        // Box the argument if it's not already a pointer
        if (!arg->getType()->isPointerTy()) {
            if (arg->getType()->isDoubleTy()) {
                auto boxFt = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { arg });
            } else if (arg->getType()->isIntegerTy(64)) {
                auto boxFt = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { arg });
            } else if (arg->getType()->isIntegerTy(1)) {
                // Convert i1 to i64 for boxing
                llvm::Value* extended = builder_->CreateZExt(arg, builder_->getInt64Ty());
                auto boxFt = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
                auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", boxFt);
                arg = builder_->CreateCall(boxFt, boxFn.getCallee(), { extended });
            }
        }
        regularArgs.push_back(arg);
    }

    // Emit ONE unified ts_call(callable, argc, a0..a8): args padded to 9
    // undefined slots, count passed explicitly (the runtime ignores slots
    // >= argc). >9 args fall back to ts_call_n's argc/argv array.
    llvm::Value* result = nullptr;
    size_t argCount = regularArgs.size();

    if (argCount <= 9) {
        llvm::Value* undef = builder_->CreateIntToPtr(
            llvm::ConstantInt::get(builder_->getInt64Ty(), 0x0A), getGCPtrTy());
        std::vector<llvm::Value*> callArgs;
        callArgs.push_back(callablePtr);
        callArgs.push_back(llvm::ConstantInt::get(builder_->getInt64Ty(), (int64_t)argCount));
        for (size_t i = 0; i < 9; ++i)
            callArgs.push_back(i < argCount ? regularArgs[i] : undef);
        std::vector<llvm::Type*> paramTypes = { getGCPtrTy(), builder_->getInt64Ty() };
        for (int i = 0; i < 9; ++i) paramTypes.push_back(getGCPtrTy());
        auto ft = llvm::FunctionType::get(getGCPtrTy(), paramTypes, false);
        auto fn = module_->getOrInsertFunction("ts_call", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), callArgs);
    } else {
        // For >9 arguments, use ts_call_n with argc/argv array
        auto arrayType = llvm::ArrayType::get(getGCPtrTy(), argCount);
        auto alloca = builder_->CreateAlloca(arrayType);
        for (size_t i = 0; i < argCount; ++i) {
            auto gep = builder_->CreateConstGEP2_32(arrayType, alloca, 0, (unsigned)i);
            builder_->CreateStore(regularArgs[i], gep);
        }
        auto argvPtr = builder_->CreateConstGEP2_32(arrayType, alloca, 0, 0);
        auto ft = llvm::FunctionType::get(getGCPtrTy(),
            { getGCPtrTy(), builder_->getInt64Ty(), getGCPtrTy() }, false);
        auto fn = module_->getOrInsertFunction("ts_call_n", ft);
        result = builder_->CreateCall(ft, fn.getCallee(), {
            callablePtr,
            llvm::ConstantInt::get(builder_->getInt64Ty(), argCount),
            argvPtr
        });
    }

    // ts_call_N returns a boxed TsValue* - we may need to unbox based on expected HIR type
    if (inst->result && inst->result->type) {
        auto expectedType = inst->result->type;

        // Unbox the result based on expected HIR type
        if (expectedType->kind == HIRTypeKind::Int64) {
            // Unbox to i64
            auto unboxFt = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_int", unboxFt);
            result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { result }, "unbox_int");
        } else if (expectedType->kind == HIRTypeKind::Float64) {
            // Unbox to f64
            auto unboxFt = llvm::FunctionType::get(builder_->getDoubleTy(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFt);
            result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { result }, "unbox_double");
        } else if (expectedType->kind == HIRTypeKind::Bool) {
            // Unbox to i1
            auto unboxFt = llvm::FunctionType::get(builder_->getInt1Ty(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFt);
            result = builder_->CreateCall(unboxFt, unboxFn.getCallee(), { result }, "unbox_bool");
        }
        // For String, Any, Object, Class - keep as ptr (already boxed TsValue*)

        setValue(inst->result, result);
    } else if (inst->result) {
        setValue(inst->result, result);
    }
}

// Call an already-resolved function value with an explicit `this` receiver.
// operands[0]=funcVal, operands[1]=thisVal, operands[2..]=raw args. Delegates
// to the single unified ts_call_with_this emitter (which boxes args, pads to 9,
// passes argc, and sets ts_last_call_argc). Replaces the by-name
// ts_call_with_this_N family that the .call() and obj[key]() sites used.
void HIRToLLVM::lowerCallValueWithThis(HIRInstruction* inst) {
    llvm::Value* funcVal = getOperandValue(inst->operands[0]);
    llvm::Value* thisArg = getOperandValue(inst->operands[1]);
    llvm::Value* result = emitDynamicMethodCall(funcVal, thisArg, inst, /*argStartIdx=*/2);
    if (inst->result) setValue(inst->result, result);
}

// Construct an instance from an already-resolved constructor value.
// operands[0]=ctorVal, operands[1..]=args. Packs argc/argv and calls the single
// unified ts_new_from_constructor entry. Replaces the by-name
// ts_new_from_constructor_N family (which also silently dropped args past 8).
void HIRToLLVM::lowerConstructFromValue(HIRInstruction* inst) {
    llvm::Value* ctorVal = getOperandValue(inst->operands[0]);
    size_t argCount = inst->operands.size() - 1;

    // Box each argument to a TsValue* for the argc/argv runtime ABI.
    std::vector<llvm::Value*> boxedArgs;
    for (size_t i = 1; i < inst->operands.size(); ++i) {
        llvm::Value* arg = getOperandValue(inst->operands[i]);
        arg = boxArgumentForDynamicCall(arg, inst->operands[i]);
        boxedArgs.push_back(arg);
    }

    llvm::Value* argvPtr;
    if (argCount == 0) {
        argvPtr = llvm::ConstantPointerNull::get(getGCPtrTy());
    } else {
        auto arrayType = llvm::ArrayType::get(getGCPtrTy(), argCount);
        auto alloca = builder_->CreateAlloca(arrayType);
        for (size_t i = 0; i < argCount; ++i) {
            auto gep = builder_->CreateConstGEP2_32(arrayType, alloca, 0, (unsigned)i);
            builder_->CreateStore(boxedArgs[i], gep);
        }
        argvPtr = builder_->CreateConstGEP2_32(arrayType, alloca, 0, 0);
    }

    auto ft = llvm::FunctionType::get(getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt32Ty(), getGCPtrTy() }, false);
    auto fn = module_->getOrInsertFunction("ts_new_from_constructor", ft);
    llvm::Value* result = builder_->CreateCall(ft, fn.getCallee(), {
        ctorVal,
        llvm::ConstantInt::get(builder_->getInt32Ty(), (int)argCount),
        argvPtr
    });
    if (inst->result) setValue(inst->result, result);
}

//==============================================================================
// Globals
//==============================================================================

// REFACTOR-NEEDED: lowerLoadGlobal
// This function has grown with many if-else branches for each global object.
// DO NOT ADD MORE CODE HERE. Instead:
// 1. Create a GlobalRegistry similar to HandlerRegistry/LoweringRegistry
// 2. Register global objects declaratively with their runtime function names
// 3. Replace this if-else chain with a registry lookup
// When you encounter this function, ASK THE USER before making changes.
void HIRToLLVM::lowerLoadGlobal(HIRInstruction* inst) {
    std::string globalName = getOperandString(inst->operands[0]);

    // For known globals, we return a sentinel pointer that the runtime recognizes
    // or call a runtime function to get the global object
    llvm::Value* result = nullptr;

    // For now, we use runtime functions for each global
    // These return a pointer to the global object
    std::string funcName = "ts_get_global_" + globalName;

    // Check for known globals and use specific runtime functions
    if (globalName == "console") {
        funcName = "ts_get_global_console";
    } else if (globalName == "Math") {
        funcName = "ts_get_global_Math";
    } else if (globalName == "JSON") {
        funcName = "ts_get_global_JSON";
    } else if (globalName == "Object") {
        funcName = "ts_get_global_Object";
    } else if (globalName == "Array") {
        funcName = "ts_get_global_Array";
    } else if (globalName == "String") {
        funcName = "ts_get_global_String";
    } else if (globalName == "Number") {
        funcName = "ts_get_global_Number";
    } else if (globalName == "Boolean") {
        funcName = "ts_get_global_Boolean";
    } else if (globalName == "Date") {
        funcName = "ts_get_global_Date";
    } else if (globalName == "RegExp") {
        funcName = "ts_get_global_RegExp";
    } else if (globalName == "Promise") {
        funcName = "ts_get_global_Promise";
    } else if (globalName == "Error") {
        funcName = "ts_get_global_Error";
    } else if (globalName == "Intl") {
        funcName = "ts_get_global_Intl";
    } else if (globalName == "Temporal") {
        funcName = "ts_get_global_Temporal";
    } else if (globalName == "Buffer") {
        funcName = "ts_get_global_Buffer";
    } else if (globalName == "Function") {
        funcName = "ts_get_global_Function";
    } else if (globalName == "TypeError") {
        funcName = "ts_get_global_TypeError";
    } else if (globalName == "RangeError") {
        funcName = "ts_get_global_RangeError";
    } else if (globalName == "ReferenceError") {
        funcName = "ts_get_global_ReferenceError";
    } else if (globalName == "SyntaxError") {
        funcName = "ts_get_global_SyntaxError";
    } else if (globalName == "URIError") {
        funcName = "ts_get_global_URIError";
    } else if (globalName == "AggregateError") {
        funcName = "ts_get_global_AggregateError";
    } else if (globalName == "EvalError") {
        funcName = "ts_get_global_EvalError";
    } else if (globalName == "ArrayBuffer") {
        funcName = "ts_get_global_ArrayBuffer";
    } else if (globalName == "DataView") {
        funcName = "ts_get_global_DataView";
    } else if (globalName == "SharedArrayBuffer") {
        funcName = "ts_get_global_SharedArrayBuffer";
    } else if (globalName == "BigInt") {
        funcName = "ts_get_global_BigInt";
    } else if (globalName == "GeneratorFunction") {
        funcName = "ts_get_global_GeneratorFunction";
    } else if (globalName == "AsyncFunction") {
        funcName = "ts_get_global_AsyncFunction";
    } else if (globalName == "AsyncGeneratorFunction") {
        funcName = "ts_get_global_AsyncGeneratorFunction";
    } else if (globalName == "Symbol") {
        funcName = "ts_get_global_Symbol";
    } else if (globalName == "Map") {
        funcName = "ts_get_global_Map";
    } else if (globalName == "Set") {
        funcName = "ts_get_global_Set";
    } else if (globalName == "WeakMap") {
        funcName = "ts_get_global_WeakMap";
    } else if (globalName == "WeakSet") {
        funcName = "ts_get_global_WeakSet";
    } else if (globalName == "WeakRef") {
        funcName = "ts_get_global_WeakRef";
    } else if (globalName == "FinalizationRegistry") {
        funcName = "ts_get_global_FinalizationRegistry";
    } else if (globalName == "Proxy") {
        funcName = "ts_get_global_Proxy";
    } else if (globalName == "Reflect") {
        funcName = "ts_get_global_Reflect";
    } else if (globalName == "TypedArray") {
        funcName = "ts_get_global_TypedArray";
    } else if (globalName == "Int8Array") {
        funcName = "ts_get_global_Int8Array";
    } else if (globalName == "Uint8Array") {
        funcName = "ts_get_global_Uint8Array";
    } else if (globalName == "Uint8ClampedArray") {
        funcName = "ts_get_global_Uint8ClampedArray";
    } else if (globalName == "Int16Array") {
        funcName = "ts_get_global_Int16Array";
    } else if (globalName == "Uint16Array") {
        funcName = "ts_get_global_Uint16Array";
    } else if (globalName == "Int32Array") {
        funcName = "ts_get_global_Int32Array";
    } else if (globalName == "Uint32Array") {
        funcName = "ts_get_global_Uint32Array";
    } else if (globalName == "Float32Array") {
        funcName = "ts_get_global_Float32Array";
    } else if (globalName == "Float64Array") {
        funcName = "ts_get_global_Float64Array";
    } else if (globalName == "BigInt64Array") {
        funcName = "ts_get_global_BigInt64Array";
    } else if (globalName == "BigUint64Array") {
        funcName = "ts_get_global_BigUint64Array";
    } else if (globalName == "process") {
        funcName = "ts_get_global_process";
    } else if (globalName == "global" || globalName == "globalThis") {
        funcName = "ts_get_global_globalThis";
    } else if (globalName == "path") {
        // path module - return a sentinel that lowerCallMethod will recognize
        // We use the global name as a marker - the runtime doesn't need an actual object
        funcName = "ts_get_global_path";
    } else if (globalName == "fs") {
        funcName = "ts_get_global_fs";
    } else if (globalName == "os") {
        funcName = "ts_get_global_os";
    } else if (globalName == "url") {
        funcName = "ts_get_global_url";
    } else if (globalName == "util") {
        funcName = "ts_get_global_util";
    } else if (globalName == "crypto") {
        funcName = "ts_get_global_crypto";
    } else if (globalName == "http") {
        funcName = "ts_get_global_http";
    } else if (globalName == "https") {
        funcName = "ts_get_global_https";
    } else if (globalName == "net") {
        funcName = "ts_get_global_net";
    } else if (globalName == "dgram") {
        funcName = "ts_get_global_dgram";
    } else if (globalName == "dns") {
        funcName = "ts_get_global_dns";
    } else if (globalName == "tls") {
        funcName = "ts_get_global_tls";
    } else if (globalName == "zlib") {
        funcName = "ts_get_global_zlib";
    } else if (globalName == "stream") {
        funcName = "ts_get_global_stream";
    } else if (globalName == "events") {
        funcName = "ts_get_global_events";
    } else if (globalName == "querystring") {
        funcName = "ts_get_global_querystring";
    // Note: "assert" is intentionally NOT mapped here. Unlike console/process/Buffer,
    // Node.js assert is NOT a global — it must be imported via require('assert').
    // A user-defined `function assert(){}` should work without collision.
    } else if (globalName == "child_process") {
        funcName = "ts_get_global_child_process";
    } else if (globalName.find("__modvar_") == 0) {
        // Module-scoped variable from an imported module
        llvm::GlobalVariable* gv = module_->getGlobalVariable(globalName);
        if (!gv) {
            gv = getOrCreateGlobal(globalName, HIRType::makeAny());
        }
        result = builder_->CreateLoad(getGCPtrTy(), gv, globalName);
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    } else if (globalName.find("_VTable_Global") != std::string::npos) {
        // VTable globals are LLVM globals, not runtime globals
        llvm::GlobalVariable* vtableGlobal = module_->getGlobalVariable(globalName);
        if (vtableGlobal) {
            result = vtableGlobal;
        } else {
            // VTable doesn't exist yet - return null
            result = llvm::ConstantPointerNull::get(getGCPtrTy());
        }
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    } else {
        // Generic global lookup
        funcName = "ts_get_global";
        // For generic globals, pass the name as an argument
        llvm::FunctionType* ft = llvm::FunctionType::get(
            getGCPtrTy(), { getGCPtrTy() }, false);
        llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
        llvm::Value* nameStr = createGlobalString(globalName);
        result = builder_->CreateCall(ft, fn.getCallee(), { nameStr });
        if (inst->result) {
            setValue(inst->result, result);
        }
        return;
    }

    // For known globals, call the specific function (no args)
    llvm::FunctionType* ft = llvm::FunctionType::get(getGCPtrTy(), false);
    llvm::FunctionCallee fn = module_->getOrInsertFunction(funcName, ft);
    result = builder_->CreateCall(ft, fn.getCallee());

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerStoreGlobal(HIRInstruction* inst) {
    std::string globalName = getOperandString(inst->operands[0]);
    llvm::Value* value = getOperandValue(inst->operands[1]);

    llvm::GlobalVariable* gv = module_->getGlobalVariable(globalName);
    if (!gv) {
        gv = getOrCreateGlobal(globalName, HIRType::makeAny());
    }

    // Box value if needed (store as ptr)
    if (value->getType() != getGCPtrTy()) {
        if (value->getType()->isIntegerTy(64)) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                getGCPtrTy(), { builder_->getInt64Ty() }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_make_int", ft);
            value = builder_->CreateCall(ft, fn.getCallee(), { value });
        } else if (value->getType()->isDoubleTy()) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                getGCPtrTy(), { builder_->getDoubleTy() }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_make_double", ft);
            value = builder_->CreateCall(ft, fn.getCallee(), { value });
        } else if (value->getType()->isIntegerTy(1)) {
            llvm::FunctionType* ft = llvm::FunctionType::get(
                getGCPtrTy(), { builder_->getInt1Ty() }, false);
            llvm::FunctionCallee fn = module_->getOrInsertFunction("ts_value_make_bool", ft);
            value = builder_->CreateCall(ft, fn.getCallee(), { value });
        }
    }

    builder_->CreateStore(value, gv);
}

// Helper: create a TsClosure for a function, set arity and display name.
// Used by lowerLoadFunction for both cached and uncached paths.

}  // namespace ts::hir
