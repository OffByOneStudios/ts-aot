#include "HIRToLLVM_Internal.h"

namespace ts::hir {


llvm::Value* HIRToLLVM::createClosureForFunction(const std::string& funcName, llvm::Function* fn) {
    // Wrap in a TsClosure so .name and .toString() work (ES2019)
    // Use a trampoline to ensure proper calling convention:
    // ts_call_N passes (closure, arg1, ...) but the original function
    // may not expect a closure context as its first parameter.
    llvm::Function* trampolineFunc = getOrCreateTrampoline(fn);
    llvm::Value* funcPtrToUse = trampolineFunc ? (llvm::Value*)trampolineFunc : (llvm::Value*)fn;

    auto closureCreateFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false);
    auto closureCreate = module_->getOrInsertFunction("ts_closure_create", closureCreateFt);
    llvm::Value* numCapturesVal = llvm::ConstantInt::get(builder_->getInt64Ty(), 0);
    llvm::Value* closure = builder_->CreateCall(closureCreateFt, closureCreate.getCallee(), { funcPtrToUse, numCapturesVal });

    // Mark as method-closure when this is a class method (HIRFunction
    // referenced from some HIRClass::methods / staticMethods). Class
    // methods have trampolines of shape `(closure, this)` that pass %1
    // directly to the method's `this` param — exactly the shape
    // ts_call_with_this_N produces in its is_method branch.
    //
    // Excluded: object-literal short-methods named `__method_X_N` —
    // their trampoline treats %0 (closure) as `this` and %1 as the
    // first user arg, so flipping them to is_method would mis-route
    // args. They're handled by the older naming-prefix check in
    // lowerLoadFunction (which sets is_method without changing the
    // trampoline). True class methods need both the flag AND the
    // trampoline's shape, which only matches HIRClass::methods.
    if (hirModule_) {
        bool isClassMethod = false;
        for (const auto& cls : hirModule_->classes) {
            for (const auto& kv : cls->methods) {
                HIRFunction* m = kv.second;
                if (m && (m->name == funcName || m->mangledName == funcName)) {
                    isClassMethod = true; break;
                }
            }
            if (isClassMethod) break;
            // NOTE: do NOT flag cls->staticMethods as is_method. A static
            // method's trampoline is `(closure, arg1, ...)` with NO `this`
            // slot — flagging it makes ts_call_with_this_N pass the receiver
            // positionally, shifting the real args by one (e.g. a class
            // EXPRESSION's `Expr.m(5)` dynamic dispatch bound the class to
            // param 0 and dropped 5). `this` inside a static still resolves
            // via the ts_call_this_value context global, which ts_call_with_this
            // sets regardless of is_method. See the matching exclusion below.
        }
        // Private INSTANCE methods (`#m`): cls->methods registers them under the
        // analyzer-mangled name `<Class>___private_<Class>_<m>`, but the closure
        // for `this.#m` (read as a value, e.g. returned from a getter) is created
        // under the unmangled `<Class>_#<m>` — so the name-match above misses them
        // and ts_call_with_this_N's non-method branch mis-routes the user arg into
        // the `this` slot (the getter-exposure arg-drop). A `#` only appears in
        // class private members, and private instance methods/accessors have the
        // (closure, this, args) trampoline, so flag them. Exclude statics, whose
        // trampoline has no `this` slot.
        if (!isClassMethod && funcName.find('#') != std::string::npos &&
            funcName.find("_static_") == std::string::npos) {
            isClassMethod = true;
        }
        // Shape-based fallback: any closure whose underlying function is
        // `this`-first (first HIR param named "this") AND for which a real
        // trampoline was created — i.e. the `(closure, this, args)` shape — is a
        // method closure. This reliably catches class accessors/methods
        // installed via the cached-closure path whose registered name doesn't
        // match cls->methods above (e.g. `C___setter_2` for `set 0b10()`), so
        // ts_call_with_this_N routes the receiver into the `this` slot instead
        // of arg0. Excludes raw object-literal accessors (getOrCreateTrampoline
        // early-returns the raw fn for the __getter_/__setter_/__method_
        // prefixes, so trampolineFunc == fn here) and statics / free functions
        // (first param is not "this").
        bool isCtor = false;
        if (!isClassMethod && trampolineFunc && trampolineFunc != fn && hirModule_) {
            // A class CONSTRUCTOR is also `this`-first with a trampoline. It
            // MUST be marked is_method — ts_function_call_with_this and
            // ts_new_from_constructor dispatch its (closure, this, ...args)
            // physical signature Convention-B (this-first); without the flag
            // they route Convention-A and `this`/arg0 shift (Promise-subclass
            // hook saw executor in the `this` slot; `new c.B()` ran the ctor
            // with this=undefined -> "Cannot set properties of undefined").
            // But it must NOT get no_prototype: that clears is_constructor, so
            // a class VALUE (`let q = C`, or `class X extends
            // <param-holding-a-class>`) fails IsConstructor — the dynamic-
            // heritage link then throws "Class extends value is not a
            // constructor" for a perfectly good class. So: is_method yes,
            // no_prototype no (see the !isCtor guard below).
            for (const auto& cls : hirModule_->classes) {
                std::string cn = cls->constructor ? cls->constructor->name
                                                  : (cls->name + "_constructor");
                if (cn == funcName) { isCtor = true; break; }
                if (cls->constructor && cls->constructor->mangledName == funcName) {
                    isCtor = true; break;
                }
            }
            for (const auto& hirFn : hirModule_->functions) {
                if ((hirFn->name == funcName || hirFn->mangledName == funcName)
                    && !hirFn->params.empty()
                    && hirFn->params[0].first == "this") {
                    isClassMethod = true;
                    break;
                }
            }
        }
        if (isClassMethod) {
            auto setMethodFt = llvm::FunctionType::get(
                builder_->getVoidTy(),
                { getGCPtrTy() }, false);
            auto setMethodFn = module_->getOrInsertFunction(
                "ts_closure_set_method", setMethodFt);
            builder_->CreateCall(setMethodFt, setMethodFn.getCallee(),
                { gcPtrToRaw(closure) });
            // A non-generator method/getter/setter is not a constructor → no own
            // `.prototype`. Generators (incl. generator methods) keep theirs.
            bool methodIsGenerator = false;
            if (hirModule_)
                for (const auto& hirFn : hirModule_->functions)
                    if (hirFn->name == funcName || hirFn->mangledName == funcName) {
                        methodIsGenerator = hirFn->isGenerator; break;
                    }
            if (!methodIsGenerator && !isCtor) {
                auto npFn = module_->getOrInsertFunction("ts_closure_set_no_prototype", setMethodFt);
                builder_->CreateCall(setMethodFt, npFn.getCallee(), { gcPtrToRaw(closure) });
            }
        }
        // Static methods/accessors are not is_method (no `this` slot) but are
        // also not constructors → no own `.prototype`. Detect by the `_static_`
        // segment, or an accessor stub (`___getter_`/`___setter_`) which, in this
        // non-is_method branch, can only be a STATIC accessor (instance accessors
        // are is_method, handled above). Exclude generators.
        else if (funcName.find("_static_") != std::string::npos ||
                 funcName.find("___getter_") != std::string::npos ||
                 funcName.find("___setter_") != std::string::npos) {
            bool sgen = false;
            if (hirModule_)
                for (const auto& hirFn : hirModule_->functions)
                    if (hirFn->name == funcName || hirFn->mangledName == funcName) { sgen = hirFn->isGenerator; break; }
            if (!sgen) {
                auto npFt = llvm::FunctionType::get(builder_->getVoidTy(), { getGCPtrTy() }, false);
                auto npFn = module_->getOrInsertFunction("ts_closure_set_no_prototype", npFt);
                builder_->CreateCall(npFt, npFn.getCallee(), { gcPtrToRaw(closure) });
            }
        }
    }

    // Set the function arity. Per ECMA-262 §10.2.5 SetFunctionLength,
    // function .length is the user-visible parameter count up to (but
    // not including) the first parameter with a default initializer,
    // a rest parameter, or a destructuring pattern. firstNonSimpleParamIndex
    // (SIZE_MAX if all params are simple) carries this from ASTToHIR.
    {
        int32_t arity = 0;
        int32_t physNumParams = 0;      // physical user params (incl. defaults)
        int32_t restParamUserIdx = -1;  // for ts_closure_set_rest_index
        bool foundHirFn = false;
        if (hirModule_) {
            for (const auto& hirFn : hirModule_->functions) {
                if (hirFn->mangledName == funcName || hirFn->name == funcName) {
                    foundHirFn = true;
                    // Count user-visible params, stopping at the first
                    // non-simple one. paramIdx tracks position in user
                    // params (excluding synthetic __closure__/__arg).
                    size_t userIdx = 0;
                    for (const auto& p : hirFn->params) {
                        if (p.first == "__closure__" || p.first == "this" ||
                            p.first.find("__arg") == 0) {
                            continue;
                        }
                        physNumParams++;  // counts params with defaults too
                        if (userIdx >= hirFn->firstNonSimpleParamIndex) {
                            continue;  // keep counting physical, stop counting .length
                        }
                        arity++;
                        userIdx++;
                    }
                    // Compute rest-param's user-visible index (excluding
                    // __closure__/this/__arg) for the runtime dispatch.
                    if (hirFn->hasRestParam) {
                        size_t uIdx = 0;
                        for (size_t i = 0; i < hirFn->params.size(); i++) {
                            const auto& p = hirFn->params[i];
                            if (p.first == "__closure__" || p.first == "this" ||
                                p.first.find("__arg") == 0) continue;
                            if (i == hirFn->restParamIndex) {
                                restParamUserIdx = (int32_t)uIdx;
                                break;
                            }
                            uIdx++;
                        }
                    }
                    break;
                }
            }
        }
        if (!foundHirFn && arity == 0 && fn) {
            // Fallback: count LLVM function params minus closure param.
            // Only used when hirModule_ lookup failed (rare — synthetic
            // functions); spec-fidelity is sacrificed here for safety.
            // Gated on !foundHirFn so a legitimately 0-arity function (e.g.
            // `function(){ return arguments.length; }`, whose padded __argN__
            // params would otherwise inflate arg_size) keeps its real .length.
            int nParams = fn->arg_size();
            if (nParams > 1) arity = nParams - 1; // minus __closure__
        }
        if (physNumParams < arity) physNumParams = arity;  // never below .length
        {
            auto setArityFt = llvm::FunctionType::get(
                builder_->getVoidTy(),
                { getGCPtrTy(), builder_->getInt32Ty() },
                false);
            auto setArityFn = module_->getOrInsertFunction("ts_closure_set_arity", setArityFt);
            builder_->CreateCall(setArityFt, setArityFn.getCallee(),
                { closure, llvm::ConstantInt::get(builder_->getInt32Ty(), arity) });
            auto setNumFn = module_->getOrInsertFunction("ts_closure_set_num_params", setArityFt);
            builder_->CreateCall(setArityFt, setNumFn.getCallee(),
                { closure, llvm::ConstantInt::get(builder_->getInt32Ty(), physNumParams) });
            // Emit ts_closure_set_rest_index when the function has a rest
            // parameter. The runtime uses this to pack trailing args into
            // a TsArray before forwarding to the fixed-arity callee.
            if (restParamUserIdx >= 0) {
                auto setRestFt = llvm::FunctionType::get(
                    builder_->getVoidTy(),
                    { getGCPtrTy(), builder_->getInt32Ty() },
                    false);
                auto setRestFn = module_->getOrInsertFunction(
                    "ts_closure_set_rest_index", setRestFt);
                builder_->CreateCall(setRestFt, setRestFn.getCallee(),
                    { closure, llvm::ConstantInt::get(
                        builder_->getInt32Ty(), restParamUserIdx) });
            }
        }
    }

    // Set the function's display name
    std::string displayName;
    bool haveExplicitName = false;  // true when an inferred/source name was set
    if (hirModule_) {
        for (const auto& hirFn : hirModule_->functions) {
            if (hirFn->mangledName == funcName || hirFn->name == funcName) {
                if (!hirFn->displayName.empty()) { displayName = hirFn->displayName; haveExplicitName = true; }
                else displayName = hirFn->name;
                break;
            }
        }
    }
    if (displayName.empty()) {
        displayName = funcName;
        auto pos = displayName.rfind("_M");
        if (pos != std::string::npos) displayName = displayName.substr(0, pos);
    }
    // Class constructor naming: the canonical symbol is "<ClassName>_constructor".
    // A class constructor's .name is the class name (or "" for an anonymous
    // class), per ECMA-262. Only derive from the mangled symbol when no explicit
    // inferred name was attached (so `var C = class{}` keeps "C", and a real
    // user function literally named `foo_constructor` is left alone).
    if (!haveExplicitName) {
        static const std::string kCtorSuffix = "_constructor";
        if (displayName.size() > kCtorSuffix.size() &&
            displayName.compare(displayName.size() - kCtorSuffix.size(),
                                kCtorSuffix.size(), kCtorSuffix) == 0) {
            std::string cn = displayName.substr(0, displayName.size() - kCtorSuffix.size());
            displayName = (cn.find("__anon_class_") == 0) ? std::string("") : cn;
        }
    }
    // For anonymous functions (arrow_fn_, fn_expr_), pass empty string
    // so the name own-property is installed with value="" per ECMA-262.
    // Without this, Object.getOwnPropertyDescriptor(fn, "name") returns
    // undefined and verifyProperty(()=>{}, "name", {value:""}) fails.
    bool isAnonymous = displayName.find("__arrow_fn_") == 0 ||
                       displayName.find("__fn_expr_") == 0;
    bool isModuleInit = displayName.find("module_init") != std::string::npos;
    if (!isModuleInit) {
        auto setNameFt = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy(), getGCPtrTy() },
            false);
        auto setNameFn = module_->getOrInsertFunction("ts_closure_set_name", setNameFt);
        auto strCreateFt = llvm::FunctionType::get(
            getGCPtrTy(),
            { getGCPtrTy() },
            false);
        auto strCreateFn = module_->getOrInsertFunction("ts_string_create", strCreateFt);
        std::string nameStr = isAnonymous ? std::string("") : displayName;
        llvm::Value* cStr = createGlobalString(nameStr);
        llvm::Value* tsStr = builder_->CreateCall(strCreateFt, strCreateFn.getCallee(), { cStr });
        builder_->CreateCall(setNameFt, setNameFn.getCallee(), { closure, tsStr });
    }

    // Box the closure so it's properly NaN-boxed as a pointer.
    // This ensures ts_typeof returns "function" and ts_extract_closure
    // can identify it via nanbox_is_ptr + magic byte check.
    auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_object",
        getGCPtrTy(), {getGCPtrTy()});
    closure = builder_->CreateCall(boxFn, {closure});

    return closure;
}

void HIRToLLVM::lowerLoadFunction(HIRInstruction* inst) {
    std::string funcName = getOperandString(inst->operands[0]);

    // Private-method value access (class DECLARATION form): the analyzer
    // mangles `#m` to `__private_<Cls>_m`, so the method BODY is emitted as
    // `<Cls>___private_<Cls>_<m>` — but the class pipeline's method table
    // still references `<Cls>_#<m>`, which exists only as an EMPTY shell
    // (ret undefined). Binding the shell made every `this.#m`-as-value read
    // (getter-exposed private methods, the ~426-test "method invoked exactly
    // once" cluster) silently no-op. Resolve to the mangled body when one
    // exists; class-EXPRESSION names (body lowered directly under
    // `<anon>_#<m>`, no mangled twin) fall through unchanged.
    {
        size_t hashPos = funcName.find("_#");
        if (hashPos != std::string::npos && hirModule_) {
            std::string cls = funcName.substr(0, hashPos);
            std::string member = funcName.substr(hashPos + 2);
            // Static private methods: shell is `<Cls>_static_#<m>` but the
            // mangled body is `<Cls>_static___private_<Cls>_<m>` (analyzer
            // mangles with the bare class name). Try the verbatim prefix
            // first, then the static-stripped class name.
            std::string baseCls = cls;
            const std::string staticSuffix = "_static";
            if (baseCls.size() > staticSuffix.size() &&
                baseCls.compare(baseCls.size() - staticSuffix.size(),
                                staticSuffix.size(), staticSuffix) == 0) {
                baseCls = baseCls.substr(0, baseCls.size() - staticSuffix.size());
            }
            std::string mangled = cls + "___private_" + cls + "_" + member;
            std::string mangledStatic = cls + "___private_" + baseCls + "_" + member;
            // Gate on the HIR function list, not the LLVM module: the mangled
            // body may not have been lowered yet (only a declaration created
            // for the vtable global) at the time this load_function runs.
            for (const auto& hirFn : hirModule_->functions) {
                if (hirFn->name == mangledStatic && mangledStatic != mangled) {
                    mangled = mangledStatic;
                }
                if (hirFn->name == mangled) {
                    if (!module_->getFunction(mangled)) {
                        // Forward-declare with the real signature so the stub
                        // path below can't squat on the name.
                        std::vector<llvm::Type*> paramTypes;
                        for (const auto& param : hirFn->params) {
                            paramTypes.push_back(getLLVMType(param.second));
                        }
                        llvm::Type* retTy = hirFn->returnType
                            ? getLLVMType(hirFn->returnType) : getGCPtrTy();
                        llvm::Function::Create(
                            llvm::FunctionType::get(retTy, paramTypes, false),
                            llvm::Function::ExternalLinkage, mangled, module_.get());
                    }
                    funcName = mangled;
                    break;
                }
            }
        }
    }

    // Look up the function in the LLVM module
    llvm::Function* fn = module_->getFunction(funcName);
    if (fn) {
        if (inst->result) {
            // Function expressions and arrow functions create a new closure each time
            // (JS spec: each evaluation produces a distinct function object).
            // Function declarations should share a single closure identity so that
            // properties set on the function are visible from all references.
            bool isFunctionExpression = (funcName.find("__arrow_fn_") == 0 ||
                                         funcName.find("__fn_expr_") == 0);

            if (!isFunctionExpression) {
                // Check the module-level closure cache
                auto it = closureCache_.find(funcName);
                if (it == closureCache_.end()) {
                    // Create a module-level global to cache this closure
                    auto* gv = new llvm::GlobalVariable(
                        *module_, getGCPtrTy(), false,
                        llvm::GlobalValue::InternalLinkage,
                        llvm::ConstantPointerNull::get(getGCPtrTy()),
                        "__closure_cache_" + funcName);
                    closureCache_[funcName] = gv;
                    it = closureCache_.find(funcName);
                }
                llvm::GlobalVariable* cacheGV = it->second;

                // Emit lazy initialization: load cached, branch if null
                llvm::Value* cached = builder_->CreateLoad(getGCPtrTy(), cacheGV, "cached_closure");
                llvm::Value* isNull = builder_->CreateICmpEQ(
                    cached, llvm::ConstantPointerNull::get(getGCPtrTy()), "closure_is_null");

                llvm::BasicBlock* currentBB = builder_->GetInsertBlock();
                llvm::BasicBlock* createBB = llvm::BasicBlock::Create(context_, "create_closure", currentFunction_);
                llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "closure_ready", currentFunction_);

                builder_->CreateCondBr(isNull, createBB, mergeBB);

                // Create closure block
                builder_->SetInsertPoint(createBB);
                llvm::Value* newClosure = createClosureForFunction(funcName, fn);
                builder_->CreateStore(newClosure, cacheGV);
                llvm::BasicBlock* createEndBB = builder_->GetInsertBlock(); // may differ after calls
                builder_->CreateBr(mergeBB);

                // Merge block with phi
                builder_->SetInsertPoint(mergeBB);
                llvm::PHINode* phi = builder_->CreatePHI(getGCPtrTy(), 2, "closure");
                phi->addIncoming(cached, currentBB);
                phi->addIncoming(newClosure, createEndBB);

                setValue(inst->result, phi);
            } else {
                // Function expression: always create a new closure (no caching)
                llvm::Value* closure = createClosureForFunction(funcName, fn);
                setValue(inst->result, closure);
            }
        }
    } else {
        // Function not found (empty body or not compiled) — generate a stub
        // that returns undefined, then create a closure wrapping it.
        SPDLOG_WARN("LoadFunction: function '{}' not found, generating stub", funcName);

        // Create stub: ptr @funcName(ptr %ctx, ptr, ptr, ptr, ptr) { ret undefined }
        auto stubFt = llvm::FunctionType::get(getGCPtrTy(),
            { getGCPtrTy(), getGCPtrTy(), getGCPtrTy(),
              getGCPtrTy(), getGCPtrTy() }, false);
        fn = llvm::Function::Create(stubFt, llvm::GlobalValue::InternalLinkage,
                                    funcName, module_.get());
        auto* bb = llvm::BasicBlock::Create(context_, "entry", fn);
        llvm::IRBuilder<> stubBuilder(bb);
        auto undefFn = module_->getOrInsertFunction("ts_value_make_undefined",
            llvm::FunctionType::get(getGCPtrTy(), {}, false));
        stubBuilder.CreateRet(stubBuilder.CreateCall(undefFn));

        if (inst->result) {
            llvm::Value* closure = createClosureForFunction(funcName, fn);
            setValue(inst->result, closure);
        }
    }
}

//==============================================================================
// Function Trampolines
//==============================================================================

llvm::Function* HIRToLLVM::getOrCreateTrampoline(llvm::Function* originalFunc) {
    if (!originalFunc) return nullptr;

    // Check if we already created a trampoline for this function
    std::string trampolineName = originalFunc->getName().str() + "$trampoline";
    if (llvm::Function* existing = module_->getFunction(trampolineName)) {
        return existing;
    }

    llvm::FunctionType* origFT = originalFunc->getFunctionType();
    unsigned numOrigParams = origFT->getNumParams();

    // Check if the function already matches the closure convention:
    // (ptr context, TsValue* args...) -> ptr
    // A function matches if: first param is ptr (context), all other params are ptr (TsValue*), returns ptr
    // AND the first param is actually a closure context (not just a user param that happens to be ptr)
    bool alreadyMatches = origFT->getReturnType()->isPointerTy();
    if (alreadyMatches && numOrigParams >= 1) {
        // First param must be context pointer
        alreadyMatches = origFT->getParamType(0)->isPointerTy();
        // All other params must be pointers (TsValue*)
        for (unsigned i = 1; i < numOrigParams && alreadyMatches; ++i) {
            alreadyMatches = origFT->getParamType(i)->isPointerTy();
        }
    }
    if (alreadyMatches && numOrigParams >= 1) {
        // Types match, but verify the first param is actually a closure context.
        // Functions like JS module functions (e.g., @add(ptr %a, ptr %b)) have all-ptr
        // params but the first param is a user param, not a closure context. Without this
        // check, ts_call_N would pass the closure as the first arg, shifting all user args.
        std::string funcName0 = originalFunc->getName().str();
        // NOTE: __method_ (object-literal shorthand methods) is deliberately
        // NOT in this list. Its param 0 is `this` — NOT a closure env — and
        // lowerLoadFunction flags these closures is_method, so the runtime
        // dispatch calls func_ptr as (closure, thisArg, args...). Returning
        // the raw fn here bound the CLOSURE to `this` and shifted every user
        // arg by one ({ m(v){ this.a = v } } saw this === the closure). They
        // fall through to the method-shaped trampoline below.
        bool isKnownClosure = (funcName0.find("__arrow_fn_") == 0) ||
                               (funcName0.find("__closure_") == 0) ||
                               (funcName0.find("__anon_fn_") == 0) ||
                               (funcName0.find("__fn_expr_") == 0) ||
                               (funcName0.find("__lambda_") == 0) ||
                               (funcName0.find("__getter_") == 0) ||
                               (funcName0.find("__setter_") == 0);
        if (!isKnownClosure) {
            auto firstArg = originalFunc->arg_begin();
            std::string firstParamName = firstArg->getName().str();
            isKnownClosure = (firstParamName == "__closure__" || firstParamName == "__closure");
        }
        if (isKnownClosure) {
            // No trampoline needed, function already has closure convention
            return originalFunc;
        }
        // Fall through to create a trampoline that strips the closure context
    }

    // Determine how many user-visible parameters the original function has
    // (i.e., parameters that aren't the closure context pointer)
    //
    // For closure functions (identified by naming convention), we know:
    // - The first parameter is ALWAYS the closure context (ptr %__closure__)
    // - ALL other parameters are user parameters, even if they're pointers (e.g., any type)
    //
    // For getter/setter functions:
    // - Getters: __getter_<prop>_<id>(ptr %this) -> first param is context, no user params
    // - Setters: __setter_<prop>_<id>(ptr %this, ptr %v) -> first param is context, second is user param
    //
    // For regular functions, we use a heuristic: count leading pointer params as context.
    std::string funcName = originalFunc->getName().str();
    bool isClosureFunction = (funcName.find("__arrow_fn_") == 0) ||
                             (funcName.find("__closure_") == 0) ||
                             (funcName.find("__anon_fn_") == 0) ||
                             (funcName.find("__fn_expr_") == 0) ||
                             (funcName.find("__lambda_") == 0);
    bool isSetterFunction = (funcName.find("__setter_") == 0);
    bool isGetterFunction = (funcName.find("__getter_") == 0);
    bool isMethodFunction = (funcName.find("__method_") == 0);

    unsigned numContextParams = 0;
    if (isClosureFunction && numOrigParams >= 1) {
        // For closures, first param is always the closure context, rest are user params
        numContextParams = 1;
    } else if (isSetterFunction && numOrigParams >= 2) {
        // For setters: first param is 'this' (context), second param is value (user param)
        numContextParams = 1;
        SPDLOG_DEBUG("getOrCreateTrampoline: detected setter function {}, using 1 context param", funcName);
    } else if (isGetterFunction && numOrigParams >= 1) {
        // For getters: first param is 'this' (context), no user params
        numContextParams = 1;
        SPDLOG_DEBUG("getOrCreateTrampoline: detected getter function {}, using 1 context param", funcName);
    } else if (isMethodFunction && numOrigParams >= 1) {
        // For methods: first param is 'this' (context), rest are user params
        // The TsFunction calling convention passes thisArg as func->context
        numContextParams = 1;
        SPDLOG_DEBUG("getOrCreateTrampoline: detected method function {}, using 1 context param", funcName);
    } else {
        // For other functions, check if the first parameter is a closure context parameter
        // Function expressions like __fn_expr_0 have a __closure__ param but don't match
        // the naming patterns above. We need to check the actual parameter name.
        bool firstParamIsClosure = false;
        if (numOrigParams >= 1) {
            auto firstArg = originalFunc->arg_begin();
            std::string firstParamName = firstArg->getName().str();
            firstParamIsClosure = (firstParamName == "__closure__" || firstParamName == "__closure");
        }

        if (firstParamIsClosure) {
            // Function has an explicit closure parameter, treat it as context
            numContextParams = 1;
            SPDLOG_DEBUG("getOrCreateTrampoline: function {} has __closure__ param, using 1 context param", funcName);
        } else {
            // For regular functions used as closures (without closure param),
            // ALL parameters are user parameters. The closure context is NOT passed to the
            // original function - it's only used by the closure mechanism itself.
            // This handles cases like:
            //   function foo(x: {a: number}): void { ... }
            //   const fn = foo;  // make_closure wraps foo
            //   fn({a: 42});     // Call via ts_call_1
            // In this case, foo's 'x' parameter is the user's argument, not a closure context.
            numContextParams = 0;
            SPDLOG_DEBUG("getOrCreateTrampoline: regular function {}, all {} params are user params", funcName, numOrigParams);
        }
    }
    unsigned numUserParams = numOrigParams - numContextParams;

    SPDLOG_DEBUG("getOrCreateTrampoline: {} has {} total params, {} context params, {} user params",
                 originalFunc->getName().str(), numOrigParams, numContextParams, numUserParams);

    // Object-literal shorthand methods (`__method_`) are is_method closures:
    // the runtime's method dispatch calls func_ptr as (closure, thisArg,
    // args...), matching the class-method trampoline shape. The original fn is
    // (this, args...) — the trampoline drops the closure slot and routes the
    // receiver into `this`.
    bool methodThisFirst = isMethodFunction && numContextParams == 1;

    // Create trampoline: (ptr %ctx, TsValue* %arg1, TsValue* %arg2, ...) -> ptr
    // (methodThisFirst adds a leading ignored closure slot before %ctx).
    // The trampoline accepts boxed arguments and returns a boxed result
    std::vector<llvm::Type*> trampolineParams;
    if (methodThisFirst) trampolineParams.push_back(getGCPtrTy());  // closure (ignored)
    trampolineParams.push_back(getGCPtrTy());  // context / this
    for (unsigned i = 0; i < numUserParams; ++i) {
        trampolineParams.push_back(getGCPtrTy());  // TsValue* for each arg
    }

    llvm::FunctionType* trampolineFT = llvm::FunctionType::get(
        getGCPtrTy(),
        trampolineParams,
        false
    );

    llvm::Function* trampoline = llvm::Function::Create(
        trampolineFT,
        llvm::GlobalValue::PrivateLinkage,
        trampolineName,
        module_.get()
    );
    if (enableGCStatepoints_) {
        trampoline->setGC("ts-aot-gc");
    }

    SPDLOG_DEBUG("getOrCreateTrampoline: creating trampoline {} for {} with {} user params",
                 trampolineName, originalFunc->getName().str(), numUserParams);

    // Save current insertion point
    llvm::BasicBlock* savedBB = builder_->GetInsertBlock();
    llvm::BasicBlock::iterator savedPt = builder_->GetInsertPoint();

    // Create entry block for trampoline
    llvm::BasicBlock* entryBB = llvm::BasicBlock::Create(context_, "entry", trampoline);
    builder_->SetInsertPoint(entryBB);

    // Build arguments for the original function call
    std::vector<llvm::Value*> callArgs;

    // Get trampoline arguments iterator
    auto trampolineArg = trampoline->arg_begin();
    if (methodThisFirst) trampolineArg++;   // Skip the ignored closure slot
    llvm::Value* ctxArg = trampolineArg++;  // Context / this argument

    // First, pass the context to all context parameters of the original function
    for (unsigned i = 0; i < numContextParams; ++i) {
        callArgs.push_back(ctxArg);
    }

    // Unbox each user argument and add to callArgs
    for (unsigned i = 0; i < numUserParams; ++i) {
        llvm::Value* boxedArg = trampolineArg++;
        llvm::Type* expectedType = origFT->getParamType(numContextParams + i);

        llvm::Value* unboxedArg;
        if (expectedType->isDoubleTy()) {
            // Unbox to double
            auto unboxFT = llvm::FunctionType::get(builder_->getDoubleTy(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_double", unboxFT);
            unboxedArg = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
        } else if (expectedType->isIntegerTy(64)) {
            // Unbox to i64
            auto unboxFT = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_int", unboxFT);
            unboxedArg = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
        } else if (expectedType->isIntegerTy(1)) {
            // Unbox to bool
            auto unboxFT = llvm::FunctionType::get(builder_->getInt64Ty(), { getGCPtrTy() }, false);
            auto unboxFn = module_->getOrInsertFunction("ts_value_get_bool", unboxFT);
            llvm::Value* boolAsInt = builder_->CreateCall(unboxFT, unboxFn.getCallee(), { boxedArg });
            unboxedArg = builder_->CreateICmpNE(boolAsInt, llvm::ConstantInt::get(builder_->getInt64Ty(), 0));
        } else if (expectedType->isPointerTy()) {
            // For pointer parameters (including 'any' type), pass the TsValue* directly.
            // The original function handles its own unboxing (e.g., calling ts_value_get_double).
            // This is critical for closures where the HIR generates unboxing code in the function body.
            unboxedArg = boxedArg;
        } else {
            // Unknown type, pass through as-is (hope it's a pointer)
            unboxedArg = boxedArg;
        }
        callArgs.push_back(unboxedArg);
    }

    // Call the original function
    llvm::Value* result = builder_->CreateCall(origFT, originalFunc, callArgs);

    // Box the result based on return type
    llvm::Value* boxedResult;
    llvm::Type* returnType = origFT->getReturnType();

    if (returnType->isPointerTy()) {
        // Already a pointer, might be TsValue* or raw object
        boxedResult = result;
    } else if (returnType->isDoubleTy()) {
        auto boxFT = llvm::FunctionType::get(getGCPtrTy(), { builder_->getDoubleTy() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_double", boxFT);
        boxedResult = builder_->CreateCall(boxFT, boxFn.getCallee(), { result });
    } else if (returnType->isIntegerTy(64)) {
        auto boxFT = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt64Ty() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_int", boxFT);
        boxedResult = builder_->CreateCall(boxFT, boxFn.getCallee(), { result });
    } else if (returnType->isIntegerTy(1)) {
        // ts_value_make_bool's canonical signature is ptr(i32) (matches
        // boxPrimitiveToPtr + ~20 other boxing sites). This trampoline was the
        // lone site declaring it ptr(i64); the mismatch is tolerated by opaque
        // pointers in the default build but makes RS4GC reject every i32 call
        // under --gc-statepoints. Use i32 here so the whole module agrees.
        llvm::Value* extended = builder_->CreateZExt(result, builder_->getInt32Ty());
        auto boxFT = llvm::FunctionType::get(getGCPtrTy(), { builder_->getInt32Ty() }, false);
        auto boxFn = module_->getOrInsertFunction("ts_value_make_bool", boxFT);
        boxedResult = builder_->CreateCall(boxFT, boxFn.getCallee(), { extended });
    } else if (returnType->isVoidTy()) {
        auto undefFT = llvm::FunctionType::get(getGCPtrTy(), {}, false);
        auto undefFn = module_->getOrInsertFunction("ts_value_make_undefined", undefFT);
        boxedResult = builder_->CreateCall(undefFT, undefFn.getCallee(), {});
    } else {
        // Fallback: return undefined
        auto undefFT = llvm::FunctionType::get(getGCPtrTy(), {}, false);
        auto undefFn = module_->getOrInsertFunction("ts_value_make_undefined", undefFT);
        boxedResult = builder_->CreateCall(undefFT, undefFn.getCallee(), {});
    }

    builder_->CreateRet(boxedResult);

    // Restore insertion point
    if (savedBB) {
        builder_->SetInsertPoint(savedBB, savedPt);
    }

    return trampoline;
}

//==============================================================================
// Closures
//==============================================================================

void HIRToLLVM::lowerMakeClosure(HIRInstruction* inst) {
    // MakeClosure creates a closure object with function pointer and captured values
    // Operand 0: function name
    // Operand 1+: captured values (to be stored in TsCells)
    //
    // Runtime: TsClosure = { func_ptr, num_captures, TsCell** cells }

    std::string funcName = getOperandString(inst->operands[0]);
    llvm::Function* fn = module_->getFunction(funcName);

    if (!fn) {
        SPDLOG_WARN("MakeClosure: function '{}' not found", funcName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(getGCPtrTy()));
        }
        return;
    }

    size_t numCaptures = inst->operands.size() - 1;

    // Declare runtime functions
    // ts_closure_create(void* funcPtr, int64_t numCaptures) -> TsClosure*
    auto closureCreateFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto closureCreate = module_->getOrInsertFunction("ts_closure_create", closureCreateFt);

    // ts_closure_init_capture(TsClosure* closure, int64_t index, TsValue* initialValue) -> void
    auto initCaptureFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy(), builder_->getInt64Ty(), getGCPtrTy() },
        false
    );
    auto initCapture = module_->getOrInsertFunction("ts_closure_init_capture", initCaptureFt);

    // ts_value_make_int(int64_t) -> TsValue*
    auto makeIntFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { builder_->getInt64Ty() },
        false
    );
    auto makeInt = module_->getOrInsertFunction("ts_value_make_int", makeIntFt);

    // ts_value_make_double(double) -> TsValue*
    auto makeDoubleFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { builder_->getDoubleTy() },
        false
    );
    auto makeDouble = module_->getOrInsertFunction("ts_value_make_double", makeDoubleFt);

    // ts_value_make_object(void*) -> TsValue*
    auto makeObjectFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto makeObject = module_->getOrInsertFunction("ts_value_make_object", makeObjectFt);

    // Create the closure: ts_closure_create(funcPtr, numCaptures)
    // The function needs a trampoline to convert from native calling convention
    // to closure calling convention: (ptr ctx, TsValue* arg1, ...) -> TsValue*
    llvm::Function* trampolineFunc = getOrCreateTrampoline(fn);
    llvm::Value* funcPtrToUse = trampolineFunc ? (llvm::Value*)trampolineFunc : (llvm::Value*)fn;

    llvm::Value* numCapturesVal = llvm::ConstantInt::get(builder_->getInt64Ty(), numCaptures);
    llvm::Value* closure = rawToGCPtr(builder_->CreateCall(closureCreateFt, closureCreate.getCallee(), { funcPtrToUse, numCapturesVal }));

    // DEBUG (compile-time gated by TS_EMIT_CLOSURE_NAMES): register the ACTUAL
    // function body `fn` (where ts_closure_get_cell calls originate) with its
    // mangled name so TS_CLOSURE_PROVENANCE can symbolicate return addresses to
    // a real source-mappable id (e.g. __fn_expr_1042). Only emitted in
    // instrumented builds → zero impact on normal compiles.
    if (std::getenv("TS_EMIT_CLOSURE_NAMES")) {
        auto regFt = llvm::FunctionType::get(
            builder_->getVoidTy(), { getGCPtrTy(), getGCPtrTy() }, false);
        auto regFn = module_->getOrInsertFunction("ts_closure_register_debug_name", regFt);
        llvm::Constant* nameStr = builder_->CreateGlobalStringPtr(funcName, "dbgfn");
        builder_->CreateCall(regFt, regFn.getCallee(), { (llvm::Value*)fn, nameStr });
    }

    // Set the function arity (user-visible parameter count for Function.length)
    {
        int32_t arity = 0;
        int32_t physNumParams = 0;
        int32_t restParamUserIdx = -1;
        bool foundHirFn = false;
        if (hirModule_) {
            for (const auto& hirFn : hirModule_->functions) {
                if (hirFn->mangledName == funcName || hirFn->name == funcName) {
                    foundHirFn = true;
                    // Same per-spec arity rule as in createClosureForFunction
                    // above — stop at first non-simple param.
                    size_t userIdx = 0;
                    for (const auto& p : hirFn->params) {
                        if (p.first == "__closure__" || p.first == "this" ||
                            p.first.find("__arg") == 0) {
                            continue;
                        }
                        physNumParams++;
                        if (userIdx >= hirFn->firstNonSimpleParamIndex) {
                            continue;
                        }
                        arity++;
                        userIdx++;
                    }
                    if (hirFn->hasRestParam) {
                        size_t uIdx = 0;
                        for (size_t i = 0; i < hirFn->params.size(); i++) {
                            const auto& p = hirFn->params[i];
                            if (p.first == "__closure__" || p.first == "this" ||
                                p.first.find("__arg") == 0) continue;
                            if (i == hirFn->restParamIndex) {
                                restParamUserIdx = (int32_t)uIdx;
                                break;
                            }
                            uIdx++;
                        }
                    }
                    break;
                }
            }
        }
        if (!foundHirFn && arity == 0 && fn) {
            // Fallback only when hirModule_ lookup failed (synthetic fns); a
            // legitimately 0-arity function (e.g. one using `arguments` with
            // padded __argN__ params) keeps its real .length.
            int nParams = fn->arg_size();
            if (nParams > 1) arity = nParams - 1;
        }
        if (physNumParams < arity) physNumParams = arity;
        auto setArityFt = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy(), builder_->getInt32Ty() },
            false);
        auto setArityFn = module_->getOrInsertFunction("ts_closure_set_arity", setArityFt);
        builder_->CreateCall(setArityFt, setArityFn.getCallee(),
            { gcPtrToRaw(closure), llvm::ConstantInt::get(builder_->getInt32Ty(), arity) });
        auto setNumFn2 = module_->getOrInsertFunction("ts_closure_set_num_params", setArityFt);
        builder_->CreateCall(setArityFt, setNumFn2.getCallee(),
            { gcPtrToRaw(closure), llvm::ConstantInt::get(builder_->getInt32Ty(), physNumParams) });
        if (restParamUserIdx >= 0) {
            auto setRestFt = llvm::FunctionType::get(
                builder_->getVoidTy(),
                { getGCPtrTy(), builder_->getInt32Ty() },
                false);
            auto setRestFn = module_->getOrInsertFunction("ts_closure_set_rest_index", setRestFt);
            builder_->CreateCall(setRestFt, setRestFn.getCallee(),
                { gcPtrToRaw(closure), llvm::ConstantInt::get(builder_->getInt32Ty(), restParamUserIdx) });
        }
        // ES IsConstructor: arrows / async / generators / async generators
        // have no [[Construct]] — `class C extends <one of them> {}` must
        // TypeError (superclass-arrow/async/generator-function family).
        // Prototype-slot semantics are separate (generators keep theirs).
        {
            bool notCtor = false;
            if (hirModule_) {
                for (const auto& hirFn : hirModule_->functions) {
                    if (hirFn->name == funcName || hirFn->mangledName == funcName) {
                        notCtor = hirFn->isAsync || hirFn->isGenerator;
                        break;
                    }
                }
            }
            if (!notCtor && funcName.rfind("__arrow_fn_", 0) == 0) notCtor = true;
            if (notCtor) {
                auto ncFt = llvm::FunctionType::get(builder_->getVoidTy(),
                                                    { getGCPtrTy() }, false);
                auto ncFn = module_->getOrInsertFunction(
                    "ts_closure_set_not_constructable", ncFt);
                builder_->CreateCall(ncFt, ncFn.getCallee(), { gcPtrToRaw(closure) });
            }
            // Arrows: mark the closure so receiver-less dispatch preserves
            // the caller's this-slot (lexical this) instead of binding
            // undefined (OrdinaryCallBindThis is ordinary-function-only).
            if (funcName.rfind("__arrow_fn_", 0) == 0) {
                auto arFt = llvm::FunctionType::get(builder_->getVoidTy(),
                                                    { getGCPtrTy() }, false);
                auto arFn = module_->getOrInsertFunction(
                    "ts_closure_set_is_arrow", arFt);
                builder_->CreateCall(arFt, arFn.getCallee(), { gcPtrToRaw(closure) });
            }
        }
    }

    // Set the function's display name on the closure for .name and .toString()
    {
        std::string displayName;
        bool haveExplicitName = false;
        if (hirModule_) {
            for (const auto& hirFn : hirModule_->functions) {
                if (hirFn->mangledName == funcName || hirFn->name == funcName) {
                    // Prefer displayName (from assignment context) over internal name
                    if (!hirFn->displayName.empty()) {
                        displayName = hirFn->displayName;
                        haveExplicitName = true;
                    } else {
                        displayName = hirFn->name;
                    }
                    break;
                }
            }
        }
        if (displayName.empty()) {
            // Fall back to funcName, strip _M0 suffix if present
            displayName = funcName;
            auto pos = displayName.rfind("_M");
            if (pos != std::string::npos) displayName = displayName.substr(0, pos);
        }
        // Class constructor: ".name" is the class name (or "" if anonymous),
        // derived from the "<ClassName>_constructor" symbol only when no explicit
        // inferred name was attached. See the matching block above.
        if (!haveExplicitName) {
            static const std::string kCtorSuffix = "_constructor";
            if (displayName.size() > kCtorSuffix.size() &&
                displayName.compare(displayName.size() - kCtorSuffix.size(),
                                    kCtorSuffix.size(), kCtorSuffix) == 0) {
                std::string cn = displayName.substr(0, displayName.size() - kCtorSuffix.size());
                displayName = (cn.find("__anon_class_") == 0) ? std::string("") : cn;
            }
        }
        // Anonymous functions (__arrow_fn_, __fn_expr_) and "anonymous"
        // get name="" so the own-property is still installed per
        // ECMA-262 (verifyProperty(()=>{}, "name", {value:""}) passes).
        bool isAnonymous = displayName.find("__arrow_fn_") == 0 ||
                           displayName.find("__fn_expr_") == 0 ||
                           displayName == "anonymous";
        bool isModuleInit = displayName.find("module_init") != std::string::npos;
        if (!isModuleInit) {
            auto setNameFt = llvm::FunctionType::get(
                builder_->getVoidTy(),
                { getGCPtrTy(), getGCPtrTy() },
                false);
            auto setNameFn = module_->getOrInsertFunction("ts_closure_set_name", setNameFt);
            auto strCreateFt = llvm::FunctionType::get(
                getGCPtrTy(),
                { getGCPtrTy() },
                false);
            auto strCreateFn = module_->getOrInsertFunction("ts_string_create", strCreateFt);
            std::string nameStr = isAnonymous ? std::string("") : displayName;
            llvm::Value* cStr = createGlobalString(nameStr);
            llvm::Value* tsStr = builder_->CreateCall(strCreateFt, strCreateFn.getCallee(), { cStr });
            builder_->CreateCall(setNameFt, setNameFn.getCallee(), { gcPtrToRaw(closure), tsStr });
        }
    }

    // Look up the inner function's captures list to get variable names
    // This allows sharing TsCells between closures that capture the same variable
    std::vector<std::string> captureNames;
    if (hirModule_) {
        for (const auto& hirFn : hirModule_->functions) {
            if (hirFn->name == funcName || hirFn->mangledName == funcName) {
                for (const auto& cap : hirFn->captures) {
                    captureNames.push_back(cap.first);
                }
                break;
            }
        }
    }

    // Tag generator/async-generator FUNCTION closures so the runtime's
    // getPrototypeOf(fn) can route to %(Async)GeneratorFunction.prototype%.
    {
        HIRFunction* targetFn = nullptr;
        for (const auto& hirFn : hirModule_->functions) {
            if (hirFn->name == funcName || hirFn->mangledName == funcName) {
                targetFn = hirFn.get();
                break;
            }
        }
        if (targetFn && targetFn->isGenerator) {
            auto setKindFt = llvm::FunctionType::get(
                builder_->getVoidTy(),
                { getGCPtrTy(), builder_->getInt32Ty() },
                false);
            auto setKind = module_->getOrInsertFunction(
                "ts_closure_set_gen_kind", setKindFt);
            builder_->CreateCall(setKindFt, setKind.getCallee(),
                { gcPtrToRaw(closure),
                  llvm::ConstantInt::get(builder_->getInt32Ty(),
                                         targetFn->isAsync ? 2 : 1) });
        }
    }

    // ts_closure_set_cell(TsClosure* closure, int64_t index, TsCell* cell) -> void
    auto setCellFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy(), builder_->getInt64Ty(), getGCPtrTy() },
        false
    );
    auto setCell = module_->getOrInsertFunction("ts_closure_set_cell", setCellFt);

    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_closure_share_or_init_cell(TsClosure*, int64_t index, TsCell** slot,
    //                               TsValue* initialValue) -> void
    // Shares one cell (held in *slot) across all closures that capture the same
    // outer variable; creates it lazily on first use. The slot is an
    // entry-block alloca so it dominates every block.
    auto shareCellFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy(), builder_->getInt64Ty(), builder_->getPtrTy(), getGCPtrTy() },
        false
    );
    auto shareCell = module_->getOrInsertFunction("ts_closure_share_or_init_cell", shareCellFt);

    // Get-or-create the per-function entry-block alloca (TsCell**) that holds
    // the canonical shared cell for a captured variable. Entry-block placement
    // makes it dominate every block; null-initialized so the first closure to
    // run creates the cell. The slot's address escapes to the runtime helper,
    // which keeps the alloca in memory (no mem2reg) so the conservative GC
    // stack scanner sees — and pins — the live cell across GC points.
    auto getOrCreateCellSlot =
        [&](const std::pair<std::string, llvm::Value*>& key) -> llvm::Value* {
        // GENERATOR impl: an entry-block alloca dies at every yield (each
        // resume re-runs impl_entry and re-nulls it), so sibling closures
        // created across a yield minted DIFFERENT cells for the same
        // generator-local. Put the slot in the ctx data buffer instead —
        // it lives as long as the generator instance and ts_alloc
        // zero-fills it, so it starts null exactly like the alloca did.
        // The GEP is re-emitted at each use point (only the INDEX is
        // cached) so dominance is never an issue across state blocks.
        if (generatorDataBuf_ && currentHIRFunction_ &&
            currentHIRFunction_->isGenerator) {
            int idx;
            auto git = generatorCellSlotIdx_.find(key);
            if (git != generatorCellSlotIdx_.end()) {
                idx = git->second;
            } else {
                idx = generatorNextCellSlotIdx_++;
                generatorCellSlotIdx_[key] = idx;
            }
            size_t base = currentHIRFunction_->params.size() +
                          (size_t)generatorLocalCount_ +
                          crossYieldSpillIds_.size();
            llvm::Value* gep = builder_->CreateGEP(getGCPtrTy(), generatorDataBuf_,
                { llvm::ConstantInt::get(builder_->getInt64Ty(),
                                         (int64_t)(base + idx)) },
                key.first + "$genslot");
            // Callers pass the slot straight to runtime calls typed `ptr`
            // (addrspace 0) -- same shape the entry-block alloca had. The
            // GEP is re-derived at every use, so the laundered pointer is
            // never live across a GC point.
            if (gep->getType() != builder_->getPtrTy()) {
                gep = builder_->CreateAddrSpaceCast(gep, builder_->getPtrTy(),
                                                    key.first + "$genslot.raw");
            }
            return gep;
        }
        auto it = capturedVarCellSlots_.find(key);
        if (it != capturedVarCellSlots_.end()) return it->second;
        llvm::AllocaInst* slot;
        {
            llvm::IRBuilder<>::InsertPointGuard guard(*builder_);
            llvm::BasicBlock* entryBB = &currentFunction_->getEntryBlock();
            builder_->SetInsertPoint(entryBB, entryBB->getFirstInsertionPt());
            slot = builder_->CreateAlloca(getGCPtrTy(), nullptr, key.first + "$cellslot");
            builder_->CreateStore(llvm::Constant::getNullValue(getGCPtrTy()), slot);
        }
        capturedVarCellSlots_[key] = slot;
        return slot;
    };

    // Box a captured value into a TsValue* per its LLVM type (the cell seed).
    auto boxCapturedValue = [&](llvm::Value* capturedValue) -> llvm::Value* {
        llvm::Type* valType = capturedValue->getType();
        if (valType->isIntegerTy(64)) {
            return builder_->CreateCall(makeIntFt, makeInt.getCallee(), { capturedValue });
        } else if (valType->isDoubleTy()) {
            return builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { capturedValue });
        } else if (valType->isIntegerTy(1)) {
            auto makeBool = getTsValueMakeBool();
            llvm::Value* extended = builder_->CreateZExt(capturedValue, builder_->getInt32Ty(), "bool_ext");
            return builder_->CreateCall(makeBool, { extended });
        } else if (valType->isIntegerTy(32)) {
            llvm::Value* extended = builder_->CreateSExt(capturedValue, builder_->getInt64Ty(), "i32_ext");
            return builder_->CreateCall(makeIntFt, makeInt.getCallee(), { extended });
        } else if (valType->isPointerTy()) {
            return builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { gcPtrToRaw(capturedValue) });
        }
        // Default: box as object (may fail for non-pointer types)
        return builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { capturedValue });
    };

    // Initialize each capture cell with its value
    for (size_t i = 0; i < numCaptures; ++i) {
        llvm::Value* capturedValue = getOperandValue(inst->operands[i + 1]);
        llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), i);

        // Check if this capture variable already has a shared cell
        std::string capVarName;
        if (i < captureNames.size()) {
            capVarName = captureNames[i];
        }

        // Transitive capture (annotated by ASTToHIR): this slot re-captures a
        // variable that is ITSELF one of the current function's captures.
        // Alias the parent's cell directly instead of copying the value —
        // the old value-copy is why closures created inside methods/getters
        // mutated a private snapshot (probe: `{ m(){ return ()=>x++ } }`).
        // The value-based chain-walk below can't recover this case once the
        // load was unboxed to a raw double/i64, so resolve it by NAME against
        // the enclosing function's capture list (same rule as LoadCapture).
        if (i < inst->captureFromParent.size() &&
            !inst->captureFromParent[i].empty() && closureParam_ &&
            currentHIRFunction_) {
            int64_t parentIdx = -1;
            for (size_t k = 0; k < currentHIRFunction_->captures.size(); ++k) {
                if (currentHIRFunction_->captures[k].first ==
                    inst->captureFromParent[i]) {
                    parentIdx = static_cast<int64_t>(k);
                    break;
                }
            }
            if (parentIdx >= 0) {
                llvm::Value* parentCell = builder_->CreateCall(
                    getCellFt, getCell.getCallee(),
                    { closureParam_,
                      llvm::ConstantInt::get(builder_->getInt64Ty(), parentIdx) });
                builder_->CreateCall(setCellFt, setCell.getCallee(),
                    { gcPtrToRaw(closure), indexVal, parentCell });
                continue;
            }
        }

        // For cell sharing, identify the source variable: if the captured value
        // was loaded from an alloca, use that alloca as the key. This ensures
        // multiple loads from the same variable share the same cell, while
        // different variables with the same name (e.g., inlined "this") don't.
        // We also trace through GC pin allocas (gc.pin*) to find the original
        // source variable, since GC pinning inserts store/reload pairs that
        // would otherwise create different sourceKeys for the same variable.
        llvm::Value* sourceKey = capturedValue;
        if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(capturedValue)) {
            sourceKey = loadInst->getPointerOperand();
            // Follow through GC pin allocas to find original source
            if (auto* pinAlloca = llvm::dyn_cast<llvm::AllocaInst>(sourceKey)) {
                if (pinAlloca->getName().starts_with("gc.pin") ||
                    pinAlloca->getName().starts_with("gc.reload")) {
                    for (auto* user : pinAlloca->users()) {
                        if (auto* storeInst = llvm::dyn_cast<llvm::StoreInst>(user)) {
                            if (storeInst->getPointerOperand() == pinAlloca) {
                                llvm::Value* storedVal = storeInst->getValueOperand();
                                if (auto* origLoad = llvm::dyn_cast<llvm::LoadInst>(storedVal)) {
                                    sourceKey = origLoad->getPointerOperand();
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        auto cellKey = std::make_pair(capVarName, sourceKey);
        llvm::BasicBlock* currentBB = builder_->GetInsertBlock();
        bool canShareCell = !capVarName.empty() && capturedVarCells_.count(cellKey) &&
                            capturedVarCells_[cellKey].second == currentBB;

        // Cross-frame cell sharing (closure-cell-by-reference, Phase 4 plan
        // Step 1+2): if the captured value chains back from a
        // `ts_cell_get(cell)` call, the variable already lives in a cell we
        // can share. This makes inner closures created inside a factory
        // share the same cell as the factory's own captured slot, so a
        // later assignment to the variable in the outermost scope (via
        // broadcastCaptureWrite -> ts_cell_set) is visible to every nested
        // closure.
        //
        // Repro this fixes: `function makeFn(){ return function(){use v} }`
        // followed by `var v = ...` later in source — the inner closure
        // used to capture v's value-at-makeFn-call-time (undefined). Now
        // it captures the SAME cell, so the later `v = ...` flows through.
        llvm::Value* existingCellFromChain = nullptr;
        {
            llvm::Value* probe = capturedValue;
            // Walk through GC pin alloca load/store pairs (the GC pinning
            // pass inserts these to keep GC roots stable across calls).
            for (int hop = 0; hop < 6 && probe; ++hop) {
                if (auto* call = llvm::dyn_cast<llvm::CallInst>(probe)) {
                    auto* callee = call->getCalledFunction();
                    if (callee && callee->getName() == "ts_cell_get" &&
                        call->arg_size() >= 1) {
                        existingCellFromChain = call->getArgOperand(0);
                        break;
                    }
                    // Unbox helpers wrap the loaded cell value; chase them.
                    if (callee && (callee->getName() == "ts_value_make_object" ||
                                   callee->getName() == "ts_value_get_object")) {
                        if (call->arg_size() >= 1) {
                            probe = call->getArgOperand(0);
                            continue;
                        }
                    }
                    break;
                }
                if (auto* loadInst = llvm::dyn_cast<llvm::LoadInst>(probe)) {
                    llvm::Value* ptr = loadInst->getPointerOperand();
                    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(ptr)) {
                        // gc.pin allocas — find the store that defined the load's value
                        if (alloca->getName().starts_with("gc.pin") ||
                            alloca->getName().starts_with("gc.reload")) {
                            llvm::Value* found = nullptr;
                            for (auto* user : alloca->users()) {
                                if (auto* st = llvm::dyn_cast<llvm::StoreInst>(user)) {
                                    if (st->getPointerOperand() == alloca) {
                                        found = st->getValueOperand();
                                        // last store wins
                                    }
                                }
                            }
                            if (found && found != probe) {
                                probe = found;
                                continue;
                            }
                        }
                    }
                    break;
                }
                break;
            }
        }

        if (existingCellFromChain) {
            // Share the existing cell directly. No boxing, no new cell.
            builder_->CreateCall(setCellFt, setCell.getCallee(),
                { gcPtrToRaw(closure), indexVal, existingCellFromChain });
            // Publish into the cross-block slot so sibling closures that capture
            // the same variable in OTHER basic blocks converge on this same cell.
            if (!capVarName.empty()) {
                llvm::Value* slot = getOrCreateCellSlot(cellKey);
                builder_->CreateStore(existingCellFromChain, slot);
                capturedVarCells_[cellKey] = { existingCellFromChain, currentBB };
            }
        } else if (!capVarName.empty() && capturedVarCells_.count(cellKey)) {
            // A PREVIOUS closure (a different MakeClosure site) already created
            // the cell for this exact binding. Make THIS closure share the very
            // same cell so writes propagate between them and to the parent.
            //   - same basic block  -> reuse the cached SSA cell directly (the
            //     legacy fast path; also the correct per-iteration cell inside a
            //     loop body, where the cached cell is re-evaluated each pass).
            //   - different block    -> load the cell from the dominating slot
            //     the first capturer published it into. ts_closure_share_or_init_cell
            //     reads *slot (the shared cell) and, only if the first capturer
            //     never ran on this path (slot still null), creates one.
            if (capturedVarCells_[cellKey].second == currentBB) {
                llvm::Value* existingCell = capturedVarCells_[cellKey].first;
                builder_->CreateCall(setCellFt, setCell.getCallee(),
                    { gcPtrToRaw(closure), indexVal, existingCell });
            } else {
                llvm::Value* boxedValue = boxCapturedValue(capturedValue);
                llvm::Value* slot = getOrCreateCellSlot(cellKey);
                builder_->CreateCall(shareCellFt, shareCell.getCallee(),
                    { gcPtrToRaw(closure), indexVal, slot, boxedValue });
                llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { gcPtrToRaw(closure), indexVal });
                capturedVarCells_[cellKey] = { cell, currentBB };
            }
        } else {
            // FIRST (or only) closure to capture this variable. Create a fresh
            // per-EXECUTION cell via ts_closure_init_capture — crucial for loop
            // bodies, where this MakeClosure is lowered once but runs each
            // iteration and must yield a distinct cell per iteration (for-let
            // semantics). Then publish the cell into the dominating slot so a
            // LATER, different closure capturing the same binding (in another
            // block) can converge on it.
            llvm::Value* boxedValue = boxCapturedValue(capturedValue);
            builder_->CreateCall(initCaptureFt, initCapture.getCallee(), { gcPtrToRaw(closure), indexVal, boxedValue });
            if (!capVarName.empty()) {
                llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { gcPtrToRaw(closure), indexVal });
                llvm::Value* slot = getOrCreateCellSlot(cellKey);
                builder_->CreateStore(cell, slot);
                capturedVarCells_[cellKey] = { cell, currentBB };
            }
        }
    }

    // Mark method closures so ts_call_with_this_N passes thisArg as the
    // first positional arg. Two cases:
    //   1. `__method_` prefixed callbacks (object literal short-methods).
    //   2. Any HIRFunction whose first parameter is named "this" — i.e.
    //      a class method like `Point_toString(ptr %this)`. Without the
    //      flag, calling `p.toString()` dynamically (when the type-
    //      analyzer hasn't inferred a known class) reaches
    //      ts_call_with_this_0's non-method branch and the method body
    //      sees `this === undefined` (its first positional param).
    bool isMethodClosure = funcName.find("__method_") == 0;
    if (!isMethodClosure && hirModule_) {
        for (const auto& hirFn : hirModule_->functions) {
            if ((hirFn->name == funcName || hirFn->mangledName == funcName)
                && !hirFn->params.empty()
                && hirFn->params[0].first == "this") {
                isMethodClosure = true;
                break;
            }
        }
    }
    if (isMethodClosure) {
        auto setMethodFt = llvm::FunctionType::get(
            builder_->getVoidTy(),
            { getGCPtrTy() },
            false);
        auto setMethodFn = module_->getOrInsertFunction("ts_closure_set_method", setMethodFt);
        builder_->CreateCall(setMethodFt, setMethodFn.getCallee(), { gcPtrToRaw(closure) });
        // Non-generator methods/getters/setters are not constructors → no `.prototype`.
        bool methodIsGenerator = false;
        if (hirModule_)
            for (const auto& hirFn : hirModule_->functions)
                if (hirFn->name == funcName || hirFn->mangledName == funcName) {
                    methodIsGenerator = hirFn->isGenerator; break;
                }
        if (!methodIsGenerator) {
            auto npFn = module_->getOrInsertFunction("ts_closure_set_no_prototype", setMethodFt);
            builder_->CreateCall(setMethodFt, npFn.getCallee(), { gcPtrToRaw(closure) });
        }
    }
    // Static methods/accessors (no `this` slot, so not isMethodClosure) are also
    // not constructors → no own `.prototype`. Detect by `_static_` or an accessor
    // stub (static accessors only, here); exclude generators.
    else if (funcName.find("_static_") != std::string::npos ||
             funcName.find("___getter_") != std::string::npos ||
             funcName.find("___setter_") != std::string::npos) {
        bool sgen = false;
        if (hirModule_)
            for (const auto& hirFn : hirModule_->functions)
                if (hirFn->name == funcName || hirFn->mangledName == funcName) { sgen = hirFn->isGenerator; break; }
        if (!sgen) {
            auto npFt = llvm::FunctionType::get(builder_->getVoidTy(), { getGCPtrTy() }, false);
            auto npFn = module_->getOrInsertFunction("ts_closure_set_no_prototype", npFt);
            builder_->CreateCall(npFt, npFn.getCallee(), { gcPtrToRaw(closure) });
        }
    }

    // Fix self-referencing closures: if a capture variable has the same name
    // as the function being closed over, update the capture cell with the
    // closure itself after creation. This handles patterns like:
    //   function router(req, res, next) { router.handle(req, res, next); }
    // where `router` captures itself.
    {
        // Get the function's display name (without module hash suffix)
        std::string closureName;
        if (hirModule_) {
            for (const auto& hirFn : hirModule_->functions) {
                if (hirFn->name == funcName || hirFn->mangledName == funcName) {
                    closureName = hirFn->displayName.empty() ? hirFn->name : hirFn->displayName;
                    break;
                }
            }
        }
        if (closureName.empty()) {
            closureName = funcName;
            auto pos = closureName.rfind("_m");
            if (pos != std::string::npos) closureName = closureName.substr(0, pos);
        }

        for (size_t i = 0; i < captureNames.size(); ++i) {
            if (captureNames[i] == closureName) {
                // Self-reference detected: update cell[i] = closure
                auto cellSetFt = llvm::FunctionType::get(
                    builder_->getVoidTy(),
                    { getGCPtrTy(), getGCPtrTy() },
                    false);
                auto cellSetFn = module_->getOrInsertFunction("ts_cell_set", cellSetFt);
                llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), i);
                llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(),
                    { gcPtrToRaw(closure), indexVal });
                llvm::Value* boxedClosure = builder_->CreateCall(makeObjectFt, makeObject.getCallee(),
                    { gcPtrToRaw(closure) });
                builder_->CreateCall(cellSetFt, cellSetFn.getCallee(), { cell, boxedClosure });
                break;
            }
        }
    }

    if (inst->result) {
        // Box the closure so it's properly NaN-boxed as a pointer.
        // This ensures ts_typeof returns "function" and ts_extract_closure
        // can identify it via nanbox_is_ptr + magic byte check.
        auto boxFn = getOrDeclareRuntimeFunction("ts_value_make_object",
            getGCPtrTy(), {getGCPtrTy()});
        llvm::Value* boxed = builder_->CreateCall(boxFn, {gcPtrToRaw(closure)});
        setValue(inst->result, rawToGCPtr(boxed));
    }
}

void HIRToLLVM::lowerLoadCapture(HIRInstruction* inst) {
    // LoadCapture loads a captured variable from the closure environment
    // Operand 0: variable name
    //
    // The closure is passed as a hidden first parameter (closureParam_)
    // We look up the index of the variable in currentHIRFunction_->captures
    // Then get the cell at that index and extract the value

    std::string varName = getOperandString(inst->operands[0]);

    if (!closureParam_) {
        SPDLOG_ERROR("LoadCapture '{}': no closure parameter available", varName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(getGCPtrTy()));
        }
        return;
    }

    // Find the index of this capture in the function's captures list
    int64_t captureIndex = -1;
    for (size_t i = 0; i < currentHIRFunction_->captures.size(); ++i) {
        if (currentHIRFunction_->captures[i].first == varName) {
            captureIndex = static_cast<int64_t>(i);
            break;
        }
    }

    if (captureIndex < 0) {
        SPDLOG_ERROR("LoadCapture: variable '{}' not found in captures list", varName);
        if (inst->result) {
            setValue(inst->result, llvm::ConstantPointerNull::get(getGCPtrTy()));
        }
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_get(TsCell* cell) -> TsValue*
    auto cellGetFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto cellGet = module_->getOrInsertFunction("ts_cell_get", cellGetFt);

    // ts_value_get_int(TsValue*) -> int64_t
    auto getIntFt = llvm::FunctionType::get(
        builder_->getInt64Ty(),
        { getGCPtrTy() },
        false
    );
    auto getInt = module_->getOrInsertFunction("ts_value_get_int", getIntFt);

    // ts_value_get_double(TsValue*) -> double
    auto getDoubleFt = llvm::FunctionType::get(
        builder_->getDoubleTy(),
        { getGCPtrTy() },
        false
    );
    auto getDouble = module_->getOrInsertFunction("ts_value_get_double", getDoubleFt);

    // ts_value_get_object(TsValue*) -> void*
    auto getObjectFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto getObject = module_->getOrInsertFunction("ts_value_get_object", getObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closureParam_, indexVal });

    // Get the boxed value from the cell: boxedValue = ts_cell_get(cell)
    llvm::Value* boxedValue = builder_->CreateCall(cellGetFt, cellGet.getCallee(), { cell });

    // Declare ts_value_get_bool(TsValue*) -> i1
    auto getBoolFt = llvm::FunctionType::get(
        builder_->getInt1Ty(),
        { getGCPtrTy() },
        false
    );
    auto getBool = module_->getOrInsertFunction("ts_value_get_bool", getBoolFt);

    // Unbox based on the expected type
    llvm::Value* result = nullptr;
    if (inst->result && inst->result->type) {
        HIRTypeKind kind = inst->result->type->kind;
        if (kind == HIRTypeKind::Int64) {
            result = builder_->CreateCall(getIntFt, getInt.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Float64) {
            result = builder_->CreateCall(getDoubleFt, getDouble.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Bool) {
            // Boolean: unbox to i1 via ts_value_get_bool
            result = builder_->CreateCall(getBoolFt, getBool.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Any) {
            // For Any type, return the boxed TsValue* as-is.
            // The consumer decides how to unbox. Using ts_value_get_object here
            // would break non-object values (e.g., timer IDs stored as NUMBER_INT).
            result = boxedValue;
        } else {
            // For pointers/objects with specific types, use ts_value_get_object
            result = builder_->CreateCall(getObjectFt, getObject.getCallee(), { boxedValue });
        }
    } else {
        // Default: return the boxed value as-is
        result = boxedValue;
    }

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerStoreCapture(HIRInstruction* inst) {
    // StoreCapture stores a value to a captured variable in the closure environment
    // Operand 0: variable name
    // Operand 1: value to store
    //
    // The closure is passed as a hidden first parameter (closureParam_)
    // We look up the index of the variable in currentHIRFunction_->captures
    // Then get the cell at that index and store the value

    std::string varName = getOperandString(inst->operands[0]);
    llvm::Value* valueToStore = getOperandValue(inst->operands[1]);

    if (!closureParam_) {
        SPDLOG_ERROR("StoreCapture '{}': no closure parameter available", varName);
        return;
    }

    // Find the index of this capture in the function's captures list
    int64_t captureIndex = -1;
    std::shared_ptr<HIRType> captureType = nullptr;
    for (size_t i = 0; i < currentHIRFunction_->captures.size(); ++i) {
        if (currentHIRFunction_->captures[i].first == varName) {
            captureIndex = static_cast<int64_t>(i);
            captureType = currentHIRFunction_->captures[i].second;
            break;
        }
    }

    if (captureIndex < 0) {
        SPDLOG_ERROR("StoreCapture: variable '{}' not found in captures list", varName);
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_set(TsCell* cell, TsValue* value) -> void
    auto cellSetFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy(), getGCPtrTy() },
        false
    );
    auto cellSet = module_->getOrInsertFunction("ts_cell_set", cellSetFt);

    // ts_value_make_int(int64_t) -> TsValue*
    auto makeIntFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { builder_->getInt64Ty() },
        false
    );
    auto makeInt = module_->getOrInsertFunction("ts_value_make_int", makeIntFt);

    // ts_value_make_double(double) -> TsValue*
    auto makeDoubleFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { builder_->getDoubleTy() },
        false
    );
    auto makeDouble = module_->getOrInsertFunction("ts_value_make_double", makeDoubleFt);

    // ts_value_make_object(void*) -> TsValue*
    auto makeObjectFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto makeObject = module_->getOrInsertFunction("ts_value_make_object", makeObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closureParam_, indexVal });

    // Box the value based on its LLVM type
    llvm::Value* boxedValue = nullptr;
    llvm::Type* valType = valueToStore->getType();

    if (valType->isIntegerTy(64)) {
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { valueToStore });
    } else if (valType->isDoubleTy()) {
        boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { valueToStore });
    } else if (valType->isIntegerTy(1)) {
        // Boolean - use ts_value_make_bool with i32
        auto makeBool = getTsValueMakeBool();
        llvm::Value* extended = builder_->CreateZExt(valueToStore, builder_->getInt32Ty(), "bool_ext");
        boxedValue = builder_->CreateCall(makeBool, { extended });
    } else if (valType->isIntegerTy(32)) {
        // i32 - extend to i64 and use makeInt
        llvm::Value* extended = builder_->CreateSExt(valueToStore, builder_->getInt64Ty(), "i32_ext");
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { extended });
    } else if (valType->isPointerTy()) {
        // For pointers/objects, box as object
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { gcPtrToRaw(valueToStore) });
    } else {
        // Default: box as object (may fail for non-pointer types)
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
    }

    // Store the value in the cell: ts_cell_set(cell, boxedValue)
    builder_->CreateCall(cellSetFt, cellSet.getCallee(), { cell, boxedValue });
}

void HIRToLLVM::lowerLoadCaptureFromClosure(HIRInstruction* inst) {
    // LoadCaptureFromClosure loads a captured variable from a specific closure
    // Operand 0: closure pointer (HIRValue)
    // Operand 1: capture index (int64_t)
    // Operand 2 (optional): fallback value for when closure is null
    // Result: the loaded value

    llvm::Value* closurePtr = getOperandValue(inst->operands[0]);
    int64_t captureIndex = getOperandInt(inst->operands[1]);

    // Check for fallback value (for paths where closure wasn't created)
    llvm::Value* fallbackVal = nullptr;
    if (inst->operands.size() > 2) {
        fallbackVal = getOperandValue(inst->operands[2]);
    }

    if (!closurePtr) {
        SPDLOG_ERROR("LoadCaptureFromClosure: closure pointer is null");
        if (inst->result) {
            setValue(inst->result, fallbackVal ? fallbackVal :
                llvm::ConstantPointerNull::get(getGCPtrTy()));
        }
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_get(TsCell* cell) -> TsValue*
    auto cellGetFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto cellGet = module_->getOrInsertFunction("ts_cell_get", cellGetFt);

    // ts_value_get_int(TsValue*) -> int64_t
    auto getIntFt = llvm::FunctionType::get(
        builder_->getInt64Ty(),
        { getGCPtrTy() },
        false
    );
    auto getInt = module_->getOrInsertFunction("ts_value_get_int", getIntFt);

    // ts_value_get_double(TsValue*) -> double
    auto getDoubleFt = llvm::FunctionType::get(
        builder_->getDoubleTy(),
        { getGCPtrTy() },
        false
    );
    auto getDouble = module_->getOrInsertFunction("ts_value_get_double", getDoubleFt);

    // ts_value_get_object(TsValue*) -> void*
    auto getObjectFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto getObject = module_->getOrInsertFunction("ts_value_get_object", getObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    // Runtime handles null closure safely (returns null cell -> null value)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closurePtr, indexVal });

    // Get the boxed value from the cell: boxedValue = ts_cell_get(cell)
    llvm::Value* boxedValue = builder_->CreateCall(cellGetFt, cellGet.getCallee(), { cell });

    // Declare ts_value_get_bool(TsValue*) -> i1
    auto getBoolFt = llvm::FunctionType::get(
        builder_->getInt1Ty(),
        { getGCPtrTy() },
        false
    );
    auto getBool = module_->getOrInsertFunction("ts_value_get_bool", getBoolFt);

    // Unbox based on the expected type
    llvm::Value* result = nullptr;
    if (inst->result && inst->result->type) {
        HIRTypeKind kind = inst->result->type->kind;
        if (kind == HIRTypeKind::Int64) {
            result = builder_->CreateCall(getIntFt, getInt.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Float64) {
            result = builder_->CreateCall(getDoubleFt, getDouble.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Bool) {
            // Boolean: unbox to i1 via ts_value_get_bool
            result = builder_->CreateCall(getBoolFt, getBool.getCallee(), { boxedValue });
        } else if (kind == HIRTypeKind::Any) {
            // For Any type, return the boxed TsValue* as-is.
            result = boxedValue;
        } else {
            // For pointers/objects with specific types, use ts_value_get_object
            result = builder_->CreateCall(getObjectFt, getObject.getCallee(), { boxedValue });
        }
    } else {
        // Default: return the boxed value as-is
        result = boxedValue;
    }

    // If a fallback value is available and types match, use select to handle
    // paths where the closure was never created (closurePtr is null).
    // The runtime null-checks prevent crashes, but the cell returns a default
    // value (0/0.0/null) instead of the original variable value.
    if (fallbackVal && result && fallbackVal->getType() == result->getType()) {
        auto* isNull = builder_->CreateICmpEQ(closurePtr,
            llvm::ConstantPointerNull::get(getGCPtrTy()), "closure_is_null");
        result = builder_->CreateSelect(isNull, fallbackVal, result, "cap.select");
    }

    if (inst->result) {
        setValue(inst->result, result);
    }
}

void HIRToLLVM::lowerStoreCaptureFromClosure(HIRInstruction* inst) {
    // StoreCaptureFromClosure stores a value to a captured variable in a specific closure
    // Operand 0: closure pointer (HIRValue)
    // Operand 1: capture index (int64_t)
    // Operand 2: value to store (HIRValue)

    llvm::Value* closurePtr = getOperandValue(inst->operands[0]);
    int64_t captureIndex = getOperandInt(inst->operands[1]);
    llvm::Value* valueToStore = getOperandValue(inst->operands[2]);

    if (!closurePtr) {
        SPDLOG_ERROR("StoreCaptureFromClosure: closure pointer is null");
        return;
    }

    // Declare runtime functions
    // ts_closure_get_cell(TsClosure* closure, int64_t index) -> TsCell*
    auto getCellFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy(), builder_->getInt64Ty() },
        false
    );
    auto getCell = module_->getOrInsertFunction("ts_closure_get_cell", getCellFt);

    // ts_cell_set(TsCell* cell, TsValue* value) -> void
    auto cellSetFt = llvm::FunctionType::get(
        builder_->getVoidTy(),
        { getGCPtrTy(), getGCPtrTy() },
        false
    );
    auto cellSet = module_->getOrInsertFunction("ts_cell_set", cellSetFt);

    // ts_value_make_int(int64_t) -> TsValue*
    auto makeIntFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { builder_->getInt64Ty() },
        false
    );
    auto makeInt = module_->getOrInsertFunction("ts_value_make_int", makeIntFt);

    // ts_value_make_double(double) -> TsValue*
    auto makeDoubleFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { builder_->getDoubleTy() },
        false
    );
    auto makeDouble = module_->getOrInsertFunction("ts_value_make_double", makeDoubleFt);

    // ts_value_make_object(void*) -> TsValue*
    auto makeObjectFt = llvm::FunctionType::get(
        getGCPtrTy(),
        { getGCPtrTy() },
        false
    );
    auto makeObject = module_->getOrInsertFunction("ts_value_make_object", makeObjectFt);

    // Get the cell: cell = ts_closure_get_cell(closure, index)
    llvm::Value* indexVal = llvm::ConstantInt::get(builder_->getInt64Ty(), captureIndex);
    llvm::Value* cell = builder_->CreateCall(getCellFt, getCell.getCallee(), { closurePtr, indexVal });

    // Box the value based on its LLVM type
    llvm::Value* boxedValue = nullptr;
    llvm::Type* valType = valueToStore->getType();

    if (valType->isIntegerTy(64)) {
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { valueToStore });
    } else if (valType->isDoubleTy()) {
        boxedValue = builder_->CreateCall(makeDoubleFt, makeDouble.getCallee(), { valueToStore });
    } else if (valType->isIntegerTy(1)) {
        // Boolean - use ts_value_make_bool with i32
        auto makeBool = getTsValueMakeBool();
        llvm::Value* extended = builder_->CreateZExt(valueToStore, builder_->getInt32Ty(), "bool_ext");
        boxedValue = builder_->CreateCall(makeBool, { extended });
    } else if (valType->isIntegerTy(32)) {
        // i32 - extend to i64 and use makeInt
        llvm::Value* extended = builder_->CreateSExt(valueToStore, builder_->getInt64Ty(), "i32_ext");
        boxedValue = builder_->CreateCall(makeIntFt, makeInt.getCallee(), { extended });
    } else if (valType->isPointerTy()) {
        // For pointers/objects, box as object
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { gcPtrToRaw(valueToStore) });
    } else {
        // Default: box as object (may fail for non-pointer types)
        boxedValue = builder_->CreateCall(makeObjectFt, makeObject.getCallee(), { valueToStore });
    }

    // Store the value in the cell: ts_cell_set(cell, boxedValue)
    builder_->CreateCall(cellSetFt, cellSet.getCallee(), { cell, boxedValue });
}

//==============================================================================
// Control Flow
//==============================================================================


}  // namespace ts::hir
