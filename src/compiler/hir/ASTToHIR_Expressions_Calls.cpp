#include "ASTToHIR_Internal.h"
#include "BuiltinRegistry.h"

namespace ts::hir {


void ASTToHIR::visitCallExpression(ast::CallExpression* node) {
    setSourceLine(node);
    if (!node) return;
    if (!node->callee) return;
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        // "use fast" struct value semantics: a struct argument is passed by
        // value (the callee gets an independent copy).
        args.push_back(maybeCloneStruct(lowerExpression(arg.get()), arg.get()));
    }

    // Spread arguments at the call site (`f(...a, b, ...c)`). Without
    // expansion, ASTToHIR would pass each spread array as a single arg
    // and the callee would see e.g. `[args[0]=arrayA, args[1]=arrayB]`
    // when it expected `[args[0]=1, args[1]=2, ...]`. visitSpreadElement
    // returns just the underlying expression's HIRValue (the array), so
    // we detect spreads here and lower to a runtime apply: build a
    // TsArray containing every argument expanded, then call
    // ts_function_apply(callee, undefined, expandedArgs).
    //
    // Excludes super() (handled below; takes its own ctor path) and
    // any case where there's no spread (preserve the fast direct-call
    // paths below).
    bool hasSpread = false;
    for (auto& arg : node->arguments) {
        if (dynamic_cast<ast::SpreadElement*>(arg.get())) { hasSpread = true; break; }
    }
    if (hasSpread && !dynamic_cast<ast::SuperExpression*>(node->callee.get())) {
        auto anyArr = HIRType::makeArray(HIRType::makeAny(), false);
        auto packed = builder_.createCall("ts_array_create", {}, anyArr);
        for (size_t i = 0; i < node->arguments.size(); ++i) {
            if (dynamic_cast<ast::SpreadElement*>(node->arguments[i].get())) {
                // SpreadElement is ITERATED (ECMA-262 ArgumentListEvaluation),
                // not concat-flattened: ts_array_concat only expanded arrays, so
                // f(...set) / f(...gen) passed the collection as ONE arg. Use
                // ts_array_spread_into (iterator protocol, handles Set/Map/gen);
                // it mutates `packed` in place and returns it.
                packed = builder_.createCall("ts_array_spread_into",
                    {packed, boxValueIfNeeded(args[i])}, anyArr);
            } else {
                builder_.createCall("ts_array_push",
                    {packed, boxValueIfNeeded(args[i])}, HIRType::makeInt64());
            }
        }
        auto calleeVal = lowerExpression(node->callee.get());
        auto undef = builder_.createConstUndefined();
        lastValue_ = builder_.createCall(
            "ts_function_apply",
            {boxValueIfNeeded(calleeVal), undef, packed},
            HIRType::makeAny());
        return;
    }

    // Handle super() call - calls parent class constructor
    auto* superExpr = dynamic_cast<ast::SuperExpression*>(node->callee.get());
    if (superExpr && currentClass_ && currentClass_->baseClass) {
        if (currentClass_->baseClass->constructor) {
            // Base class has explicit constructor - call it with [this, ...args]
            // Truncate or pad args to match the base constructor's arity:
            // verifier rejects extra args (super(1,2) on a zero-arg base),
            // and missing args are undefined.
            HIRFunction* baseCtor = currentClass_->baseClass->constructor;
            // Param 0 of a constructor is `this` (implicit), so user-visible
            // arity is params.size() - 1.
            size_t expectedUserArgs = baseCtor->params.empty() ? 0 : baseCtor->params.size() - 1;
            std::vector<std::shared_ptr<HIRValue>> ctorArgs;
            auto thisVal = lookupVariable("this");
            if (thisVal) {
                ctorArgs.push_back(thisVal);
            } else {
                ctorArgs.push_back(builder_.createConstNull());
            }
            if (baseCtor->hasRestParam) {
                for (auto& arg : args) ctorArgs.push_back(arg);
            } else {
                for (size_t i = 0; i < expectedUserArgs; ++i) {
                    if (i < args.size()) ctorArgs.push_back(args[i]);
                    else ctorArgs.push_back(builder_.createConstUndefined());
                }
            }
            builder_.createCall(baseCtor->name, ctorArgs, HIRType::makeVoid());
        }
        // If base class has no explicit constructor (e.g., abstract class),
        // super() is a no-op - just continue with the derived class constructor
        lastValue_ = builder_.createConstUndefined();
        return;
    }

    // `super[key](...)` with a literal key: resolve to the base-class method and
    // dispatch (instance: with `this`; static: args only). Mirrors super.method().
    if (auto* eaCallee = dynamic_cast<ast::ElementAccessExpression*>(node->callee.get())) {
        if (dynamic_cast<ast::SuperExpression*>(eaCallee->expression.get()) &&
            currentClass_ && currentClass_->baseClass) {
            std::string key;
            if (auto* sl = dynamic_cast<ast::StringLiteral*>(eaCallee->argumentExpression.get()))
                key = sl->value;
            else if (auto* nl = dynamic_cast<ast::NumericLiteral*>(eaCallee->argumentExpression.get()))
                key = std::to_string((int64_t)nl->value);
            if (!key.empty()) {
                bool inStatic = currentFunction_ &&
                    currentFunction_->name.find("_static_") != std::string::npos;
                std::vector<std::shared_ptr<HIRValue>> callArgs;
                for (auto& a : node->arguments) callArgs.push_back(lowerExpression(a.get()));
                auto thisVal = lookupVariable("this");
                if (!thisVal) thisVal = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                for (HIRClass* sc = currentClass_->baseClass; sc; sc = sc->baseClass) {
                    auto& tbl = inStatic ? sc->staticMethods : sc->methods;
                    auto it = tbl.find(key);
                    if (it != tbl.end() && it->second) {
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        if (!inStatic) methodArgs.push_back(thisVal);
                        for (auto& a : callArgs) methodArgs.push_back(a);
                        builder_.createCall("ts_set_last_call_argc",
                            {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                        lastValue_ = builder_.createCall(it->second->name, methodArgs, it->second->returnType);
                        return;
                    }
                }
            }
        }
    }

    // Handle method call
    auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->callee.get());
    if (propAccess) {
        // Case 0: Namespace method call - import * as ns from './mod'; ns.func()
        // Check specializations (always complete) to determine if this is a user-defined
        // function. module_->functions may not have the function yet if the specialization
        // hasn't been processed, but specializations_ is set at the start.
        // Extension modules (timers/promises, fs, etc.) don't have specializations,
        // so they fall through to the normal dispatch path.
        if (propAccess->expression->inferredType &&
            propAccess->expression->inferredType->kind == ts::TypeKind::Namespace) {
            std::string funcName = propAccess->name;

            // Compute mangled name based on argument types
            std::vector<std::shared_ptr<ts::Type>> argTypes;
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType ? arg->inferredType
                                   : std::make_shared<ts::Type>(ts::TypeKind::Any));
            }
            // Get module path from namespace type for cross-module disambiguation
            std::string nsModulePath;
            if (auto nsType = std::dynamic_pointer_cast<ts::NamespaceType>(propAccess->expression->inferredType)) {
                if (nsType->module) {
                    nsModulePath = nsType->module->path;
                }
            }
            std::string mangledName = Monomorphizer::generateMangledName(
                funcName, argTypes, node->resolvedTypeArguments, nsModulePath);

            // Check specializations to determine if this is a user-defined function
            bool foundSpec = false;
            std::string callName = mangledName;
            std::shared_ptr<ts::Type> specReturnType;

            if (specializations_) {
                // Try mangled name first
                for (const auto& spec : *specializations_) {
                    if (spec.specializedName == mangledName) {
                        foundSpec = true;
                        specReturnType = spec.returnType;
                        break;
                    }
                }
                // Try original name as fallback, but skip class methods.
                // Class methods (spec.classType != null) have originalName matching
                // their method name (e.g., "inc" for SemVer.inc), which can collide
                // with standalone module functions of the same name.
                if (!foundSpec) {
                    for (const auto& spec : *specializations_) {
                        if (spec.originalName == funcName && spec.specializedName != funcName
                            && !spec.classType) {
                            foundSpec = true;
                            callName = spec.specializedName;
                            specReturnType = spec.returnType;
                            break;
                        }
                    }
                }
            }

            // EVAL-001 Phase 2: a globalThis-backed fn binding may have
            // been REBOUND by eval — never call the static specialization;
            // fall through to the value-based call path.
            if (foundSpec && module_->globalObjectVars.count(modVarName(funcName)))
                foundSpec = false;
            if (foundSpec) {
                // Look up HIR function for parameter info (may not be available yet
                // if this function's specialization hasn't been processed)
                HIRFunction* targetFunc = nullptr;
                for (auto& f : module_->functions) {
                    if (f->name == callName) {
                        targetFunc = f.get();
                        break;
                    }
                }

                // Pad args with undefined for missing params
                if (targetFunc && args.size() < targetFunc->params.size()) {
                    for (size_t i = args.size(); i < targetFunc->params.size(); ++i) {
                        args.push_back(builder_.createConstUndefined());
                    }
                }

                // Box arguments when target parameter is Any type
                if (targetFunc) {
                    for (size_t i = 0; i < args.size() && i < targetFunc->params.size(); ++i) {
                        const auto& [paramName, paramType] = targetFunc->params[i];
                        if (paramType && paramType->kind == HIRTypeKind::Any) {
                            args[i] = boxValueIfNeeded(args[i]);
                        }
                    }
                }

                // Determine return type from HIR function or specialization
                std::shared_ptr<HIRType> returnType;
                if (targetFunc && targetFunc->returnType) {
                    returnType = targetFunc->returnType;
                } else if (specReturnType) {
                    returnType = convertType(specReturnType);
                } else {
                    returnType = HIRType::makeAny();
                }

                lastValue_ = builder_.createCall(callName, args, returnType);
                return;
            }
            // If not found in specializations, check if this is a CJS namespace import.
            // CJS namespace imports (e.g., import * as ns from './cjs-module') store the
            // module.exports object in moduleGlobalVars_. We must use explicit
            // GetPropDynamic + CallIndirect instead of createCallMethod, because
            // createCallMethod's built-in method matching (e.g., "add" -> Set.add) can
            // incorrectly intercept common method names.
            auto* nsIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
            if (nsIdent && isModuleGlobalVar(nsIdent->name)) {
                auto obj = lowerExpression(propAccess->expression.get());
                auto func = builder_.createGetPropStatic(obj, funcName, HIRType::makeAny());
                // Box all arguments for dynamic call
                std::vector<std::shared_ptr<HIRValue>> boxedArgs;
                for (auto& arg : args) {
                    boxedArgs.push_back(boxValueIfNeeded(arg));
                }
                lastValue_ = builder_.createCallIndirect(func, boxedArgs, HIRType::makeAny());
                return;
            }
            // Otherwise fall through to normal dispatch
        }

        // Check if we can use a direct call for method invocation

        // Case super: `super.method(...)` inside a class method. Walk the
        // base-class chain (skipping currentClass_ itself, so an override
        // doesn't shadow the parent's implementation) and emit a direct
        // call to the resolved method with `this` from the current scope.
        // ECMA-262 §13.3.7 GetSuperBase: super resolves to the home
        // object's [[Prototype]], which for class methods is the parent
        // prototype.
        auto* superRecv = dynamic_cast<ast::SuperExpression*>(propAccess->expression.get());
        if (superRecv && currentClass_ && currentClass_->baseClass) {
            // Static context: `super.m()` inside a static method dispatches to the
            // base class's STATIC method (which takes only args, no `this`), not
            // an instance method. Without this, the instance search below missed
            // and fell through to dynamic `this`-dispatch, crashing.
            bool inStatic = currentFunction_ &&
                currentFunction_->name.find("_static_") != std::string::npos;
            if (inStatic) {
                for (HIRClass* sc = currentClass_->baseClass; sc; sc = sc->baseClass) {
                    auto sit = sc->staticMethods.find(propAccess->name);
                    if (sit != sc->staticMethods.end() && sit->second) {
                        builder_.createCall("ts_set_last_call_argc",
                            {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                        lastValue_ = builder_.createCall(sit->second->name, args, sit->second->returnType);
                        return;
                    }
                }
            }
            HIRClass* searchClass = currentClass_->baseClass;
            while (searchClass) {
                auto it = searchClass->methods.find(propAccess->name);
                if (it != searchClass->methods.end() && it->second) {
                    HIRFunction* method = it->second;
                    std::vector<std::shared_ptr<HIRValue>> methodArgs;
                    auto thisVal = lookupVariable("this");
                    methodArgs.push_back(thisVal ? thisVal : builder_.createCall("ts_get_call_this", {}, HIRType::makeAny()));
                    for (auto& arg : args) methodArgs.push_back(arg);
                    auto resultType = method->returnType;
                    if (method->isGenerator) {
                        resultType = HIRType::makeClass("Generator", 0);
                    }
                    builder_.createCall("ts_set_last_call_argc",
                        {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                    lastValue_ = builder_.createCall(method->name, methodArgs, resultType);
                    return;
                }
                searchClass = searchClass->baseClass;
            }
            // Method not found in any user-defined base class. Fall back
            // to dynamic dispatch on `this` — this handles methods
            // inherited from a runtime/extension base (e.g. EventEmitter).
            auto obj = lookupVariable("this");
            if (!obj) obj = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
            lastValue_ = builder_.createCallMethod(obj, resolvePrivateName(propAccess->name), args, HIRType::makeAny());
            return;
        }

        // Case 1: Method call on 'this' - we know the class statically
        auto* thisIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        // PRIVATE method call on `this`: the receiver is NOT provably an
        // instance — c.method.call(foreignObj) rebinds it (the inner-arrow-
        // function family). Brand-check via ts_object_get_private (the
        // installed "#m@Class" method resolves on real instances; a foreign
        // receiver throws TypeError) and call the resolved value.
        if (thisIdent && thisIdent->name == "this" && currentClass_ &&
            !propAccess->name.empty() && propAccess->name[0] == '#') {
            // Lower `this` through the standard identifier path — inside an
            // ARROW it resolves through the closure-capture machinery;
            // lookupVariable("this") returned the raw closure param there
            // (receiver arrived as the CLSR object, TS_DEBUG_PRIVGET trace).
            auto objV = lowerExpression(propAccess->expression.get());
            auto keyStr = builder_.createConstString(resolvePrivateName(propAccess->name));
            auto boxedObj = boxValueIfNeeded(objV);
            auto func = builder_.createCall("ts_object_get_private",
                {boxedObj, keyStr}, HIRType::makeAny());
            lastValue_ = builder_.createCallWithThis(func, boxedObj, args,
                                                     HIRType::makeAny());
            return;
        }
        if (thisIdent && thisIdent->name == "this" && currentClass_) {
            // Look up the method in the current class
            auto it = currentClass_->methods.find(propAccess->name);
            if (it != currentClass_->methods.end()) {
                HIRFunction* method = it->second;
                fprintf(stderr, "  Case1: this.%s -> method=%p name=%s\n",
                    propAccess->name.c_str(), (void*)method,
                    method ? method->name.c_str() : "null");
                fflush(stderr);
                if (!method) {
                    // Placeholder method - construct name
                    std::string methodFuncName = currentClass_->name + "_" + propAccess->name;
                    auto obj = lowerExpression(propAccess->expression.get());
                    std::vector<std::shared_ptr<HIRValue>> methodArgs;
                    methodArgs.push_back(obj);
                    for (auto& arg : args) methodArgs.push_back(arg);
                    builder_.createCall("ts_set_last_call_argc",
                        {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                    lastValue_ = builder_.createCall(methodFuncName, methodArgs, HIRType::makeAny());
                    return;
                }
                // Build args: [this, ...args]
                std::vector<std::shared_ptr<HIRValue>> methodArgs;
                auto thisVal = lookupVariable("this");
                if (thisVal) {
                    methodArgs.push_back(thisVal);
                } else {
                    // `this` not a scope variable (static-method body or
                    // detached context): use the dynamic call-this, same as
                    // the identifier path. Const-null here made every
                    // this.<member> call in static methods throw
                    // TypeError-on-null.
                    methodArgs.push_back(builder_.createCall(
                        "ts_get_call_this", {}, HIRType::makeAny()));
                }
                for (auto& arg : args) {
                    methodArgs.push_back(arg);
                }
                // Direct call to the method function
                // Generator methods return Generator, not the method's declared return type
                auto resultType = method->returnType;
                if (method->isGenerator) {
                    resultType = HIRType::makeClass("Generator", 0);
                }
                builder_.createCall("ts_set_last_call_argc",
                    {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                lastValue_ = builder_.createCall(method->name, methodArgs, resultType);
                return;
            } else {
                // Method not found in current class. Check if it's:
                // 1. An abstract method → dynamic dispatch via vtable
                // 2. Inherited from a user-defined base class → direct call
                // 3. Inherited from a runtime/extension base class → dynamic dispatch

                // Check abstract methods first
                if (currentClass_->abstractMethods.count(propAccess->name)) {
                    auto obj = lookupVariable("this");
                    if (!obj) obj = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                    lastValue_ = builder_.createCallMethod(obj, resolvePrivateName(propAccess->name), args, HIRType::makeAny());
                    return;
                }

                // Walk base class chain for user-defined inherited methods
                HIRClass* searchClass = currentClass_->baseClass;
                while (searchClass) {
                    auto baseIt = searchClass->methods.find(propAccess->name);
                    if (baseIt != searchClass->methods.end() && baseIt->second) {
                        // Found in user-defined base class - direct call
                        HIRFunction* method = baseIt->second;
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        auto thisVal = lookupVariable("this");
                        methodArgs.push_back(thisVal ? thisVal : builder_.createCall("ts_get_call_this", {}, HIRType::makeAny()));
                        for (auto& arg : args) methodArgs.push_back(arg);
                        auto resultType = method->returnType;
                        if (method->isGenerator) {
                            resultType = HIRType::makeClass("Generator", 0);
                        }
                        builder_.createCall("ts_set_last_call_argc",
                            {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                        lastValue_ = builder_.createCall(method->name, methodArgs, resultType);
                        return;
                    }
                    searchClass = searchClass->baseClass;
                }

                // Not found in any user class - use dynamic dispatch.
                // This handles methods inherited from runtime/extension base
                // classes (e.g., Counter extends EventEmitter → this.emit()).
                auto obj = lookupVariable("this");
                if (!obj) obj = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                lastValue_ = builder_.createCallMethod(obj, resolvePrivateName(propAccess->name), args, HIRType::makeAny());
                return;
            }
        }

        // Case 2: Check if object has a known class type from inference
        std::string className;
        if (propAccess->expression->inferredType) {
            auto& type = propAccess->expression->inferredType;
            if (type->kind == ts::TypeKind::Class) {
                auto classType = std::dynamic_pointer_cast<ts::ClassType>(type);
                if (classType) {
                    className = classType->name;
                }
            }
        }
        // Also check: if the expression is a NewExpression for a known class,
        // use direct VTable dispatch. This handles patterns like:
        //   new SemVer(a).compare(new SemVer(b))
        // where the type analyzer hasn't set inferredType on the NewExpression.
        if (className.empty()) {
            auto* newExpr = dynamic_cast<ast::NewExpression*>(propAccess->expression.get());
            if (newExpr) {
                auto* newIdent = dynamic_cast<ast::Identifier*>(newExpr->expression.get());
                if (newIdent) {
                    for (auto& cls : module_->classes) {
                        if (cls->name == newIdent->name) {
                            className = newIdent->name;
                            break;
                        }
                    }
                    // Class-expression binding: `var C = class { ... }`
                    // stores `__anon_class_N` under variableToClassName_["C"].
                    // The direct-name search above finds nothing (class is
                    // anonymous); consult the map. Only used as a fallback
                    // so real class declarations take the natural-name path.
                    // visitNewExpression already does this lookup at the
                    // construct site; Case 2 method-dispatch must do it too
                    // or we fall through to dynamic prototype lookup which
                    // can't find vtable methods on the FLAT instance.
                    if (className.empty()) {
                        auto vIt = variableToClassName_.find(newIdent->name);
                        if (vIt != variableToClassName_.end()) {
                            for (auto& cls : module_->classes) {
                                if (cls->name == vIt->second) {
                                    className = vIt->second;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!className.empty()) {
            // Look up the class and search up the inheritance chain
            bool foundInUserClass = false;
            for (auto& cls : module_->classes) {
                if (cls->name == className) {
                    // Search in this class and all base classes
                    HIRClass* searchClass = cls.get();
                    while (searchClass) {
                        auto it = searchClass->methods.find(propAccess->name);
                        if (it != searchClass->methods.end()) {
                            HIRFunction* method = it->second;
                            fprintf(stderr, "  Case2: %s.%s -> method=%p\n",
                                className.c_str(), propAccess->name.c_str(), (void*)method);
                            fflush(stderr);
                            // Determine function name and return type.
                            // method may be nullptr (pre-registered placeholder from spec pre-pass)
                            std::string methodFuncName;
                            auto resultType = HIRType::makeAny();
                            if (method) {
                                methodFuncName = method->name;
                                resultType = method->returnType;
                                if (method->isGenerator) {
                                    resultType = HIRType::makeClass("Generator", 0);
                                }
                            } else {
                                // Placeholder - construct name from convention
                                methodFuncName = searchClass->name + "_" + propAccess->name;
                            }
                            // Build args: [obj, ...args]
                            auto obj = lowerExpression(propAccess->expression.get());
                            std::vector<std::shared_ptr<HIRValue>> methodArgs;
                            methodArgs.push_back(obj);
                            for (auto& arg : args) {
                                methodArgs.push_back(arg);
                            }
                            builder_.createCall("ts_set_last_call_argc",
                                {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                            lastValue_ = builder_.createCall(methodFuncName, methodArgs, resultType);
                            return;
                        }
                        // Move to base class
                        searchClass = searchClass->baseClass;
                    }
                    foundInUserClass = true; // Class exists but method not found
                    break;
                }
            }

            // Case 2b: Extension class instance method call.
            // Only for types with kind == "class" (have real standalone C functions).
            // Types with kind == "interface" (Stats, Dirent) use closure-based dispatch.
            // Skip if className comes from a user-imported module (moduleGlobalVars_).
            // This prevents user-defined classes (e.g., eventemitter3's EventEmitter)
            // from being dispatched to the runtime's built-in extension methods.
            if (!foundInUserClass && !isModuleGlobalVar(className)) {
                auto& extReg = ext::ExtensionRegistry::instance();

                // Check for static methods FIRST when expression is a bare identifier
                // matching a type/global name (e.g., Response.json() vs resp.json()).
                // This prevents static methods from being shadowed by instance methods.
                auto* bareIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
                if (bareIdent && extReg.isClassKind(bareIdent->name)) {
                    const ext::MethodDefinition* extStaticMethod = extReg.findStaticMethod(bareIdent->name, propAccess->name);
                    if (extStaticMethod && extStaticMethod->lowering) {
                        std::string funcName = extStaticMethod->hirName.value_or(extStaticMethod->call);
                        auto resultType = extTypeRefToHIR(extStaticMethod->returns);
                        lastValue_ = builder_.createCall(funcName, args, resultType);
                        return;
                    }
                }

                if (extReg.isClassKind(className)) {
                    const ext::MethodDefinition* extMethod = extReg.findMethod(className, propAccess->name);
                    if (extMethod && extMethod->lowering) {
                        std::string funcName = extMethod->hirName.value_or(extMethod->call);
                        auto obj = lowerExpression(propAccess->expression.get());
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        methodArgs.push_back(obj);
                        for (auto& arg : args) {
                            methodArgs.push_back(arg);
                        }
                        // Map ext.json return type to HIR type for proper downstream handling
                        auto resultType = extTypeRefToHIR(extMethod->returns);
                        lastValue_ = builder_.createCall(funcName, methodArgs, resultType);
                        return;
                    }
                }

                // Case 2c: Built-in WeakRef/FinalizationRegistry instance methods
                if (className == "WeakRef" && propAccess->name == "deref") {
                    auto obj = lowerExpression(propAccess->expression.get());
                    lastValue_ = builder_.createCall("ts_weakref_deref", {obj}, HIRType::makeAny());
                    return;
                }
                if (className == "FinalizationRegistry") {
                    if (propAccess->name == "register") {
                        auto obj = lowerExpression(propAccess->expression.get());
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        methodArgs.push_back(obj);
                        for (auto& arg : args) {
                            methodArgs.push_back(arg);
                        }
                        // Pad to 4 args (registry, target, heldValue, unregisterToken)
                        while (methodArgs.size() < 4) {
                            methodArgs.push_back(builder_.createConstUndefined());
                        }
                        builder_.createCall("ts_finalization_registry_register", methodArgs, HIRType::makeVoid());
                        lastValue_ = builder_.createConstUndefined();
                        return;
                    }
                    if (propAccess->name == "unregister") {
                        auto obj = lowerExpression(propAccess->expression.get());
                        std::vector<std::shared_ptr<HIRValue>> methodArgs;
                        methodArgs.push_back(obj);
                        for (auto& arg : args) {
                            methodArgs.push_back(arg);
                        }
                        lastValue_ = builder_.createCall("ts_finalization_registry_unregister", methodArgs, HIRType::makeBool());
                        return;
                    }
                }
            }
        }

        // Case 3: Static method call - ClassName.methodName(...)
        auto* classNameIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get());
        if (classNameIdent) {
            // Check if this is a class name
            for (auto& cls : module_->classes) {
                if (cls->name == classNameIdent->name) {
                    // Check for static method (raw name first, then
                    // __getter_<name> for static accessors which are now
                    // routed via methodKey for runtime dispatch).
                    auto it = cls->staticMethods.find(propAccess->name);
                    if (it == cls->staticMethods.end()) {
                        it = cls->staticMethods.find("__getter_" + propAccess->name);
                    }
                    if (it != cls->staticMethods.end()) {
                        HIRFunction* method = it->second;
                        // Static getter: invoke the getter with the class as `this`,
                        // then call the returned value with the user's args. Direct-
                        // calling the getter with `args` produces an arity mismatch
                        // and an LLVM verifier failure. Save/restore call-this to
                        // avoid leaking the receiver into subsequent calls.
                        if (method && method->name.find("___getter_") != std::string::npos) {
                            auto classObj = lowerExpression(propAccess->expression.get());
                            auto boxedClass = boxValueIfNeeded(classObj);
                            auto savedThis = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                            builder_.createCall("ts_set_call_this", {boxedClass}, HIRType::makeVoid());
                            auto returnedFn = builder_.createCall(method->name, {},
                                method->returnType ? method->returnType : HIRType::makeAny());
                            builder_.createCall("ts_set_call_this", {savedThis}, HIRType::makeVoid());
                            std::vector<std::shared_ptr<HIRValue>> callArgs;
                            for (auto& a : args) callArgs.push_back(boxValueIfNeeded(a));
                            lastValue_ = builder_.createCallIndirect(
                                boxValueIfNeeded(returnedFn), callArgs, HIRType::makeAny());
                            return;
                        }
                        // Static methods take no 'this' parameter, but their
                        // BODIES read `this` via ts_get_call_this() (ECMA-262:
                        // `this` in a static method is the constructor —
                        // `static x() { return this.#x(84) }` is the test262
                        // private-statics idiom). Set call-this to the class
                        // for the duration of the call, mirroring the static-
                        // getter path above; previously it was left null and
                        // every this.<member> read threw TypeError-on-null.
                        // Truncate or pad args to match the callee's arity:
                        // verifier rejects extra args, and missing args
                        // need explicit `undefined` so the receiver always
                        // sees the same shape.
                        std::vector<std::shared_ptr<HIRValue>> calleeArgs;
                        size_t expected = method->params.size();
                        if (method->hasRestParam && expected > 0) {
                            // Keep all user args; the rest-param lowering
                            // collects the trailing values into an array.
                            calleeArgs = args;
                        } else {
                            for (size_t i = 0; i < expected; ++i) {
                                if (i < args.size()) calleeArgs.push_back(args[i]);
                                else calleeArgs.push_back(builder_.createConstUndefined());
                            }
                        }
                        auto classObjV = lowerExpression(propAccess->expression.get());
                        auto boxedClassV = boxValueIfNeeded(classObjV);
                        auto savedThisV = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                        builder_.createCall("ts_set_call_this", {boxedClassV}, HIRType::makeVoid());
                        builder_.createCall("ts_set_last_call_argc",
                            {builder_.createConstInt((int64_t)node->arguments.size())}, HIRType::makeVoid());
                        lastValue_ = builder_.createCall(method->name, calleeArgs, method->returnType);
                        builder_.createCall("ts_set_call_this", {savedThisV}, HIRType::makeVoid());
                        return;
                    }
                    // Object.prototype methods are inherited by every class
                    // constructor via Function.prototype → Object.prototype.
                    // Calls like `C.hasOwnProperty(...)` should go through
                    // dynamic dispatch on the class object so the prototype
                    // chain resolves them at runtime — emitting the user-
                    // class-static convention (`C_static_hasOwnProperty`)
                    // would yield an undefined-symbol linker error.
                    static const std::set<std::string> objectProtoMethods = {
                        // Object.prototype methods inherited via Function.prototype
                        "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable",
                        "toString", "toLocaleString", "valueOf",
                        // Function.prototype methods on the class constructor
                        "bind", "call", "apply",
                    };
                    if (objectProtoMethods.count(propAccess->name)) {
                        auto obj = lowerExpression(propAccess->expression.get());
                        lastValue_ = builder_.createCallMethod(obj, resolvePrivateName(propAccess->name), args, HIRType::makeAny());
                        return;
                    }
                    // ECMA-262 §15.7.14: a static method not defined on this
                    // class may be INHERITED from a base class via the
                    // constructor proto chain. Walk the base chain and dispatch
                    // directly to the ancestor's static method (the dynamic
                    // path already resolves `D[k]()`; this covers the literal
                    // `Derived.bm()` call). Non-getter methods only — static
                    // accessor inheritance falls through to the dynamic path.
                    for (HIRClass* anc = cls->baseClass; anc; anc = anc->baseClass) {
                        auto bit = anc->staticMethods.find(propAccess->name);
                        if (bit == anc->staticMethods.end() || !bit->second) continue;
                        HIRFunction* bmethod = bit->second;
                        if (bmethod->name.find("___getter_") != std::string::npos) break;
                        std::vector<std::shared_ptr<HIRValue>> calleeArgs;
                        size_t expected = bmethod->params.size();
                        if (bmethod->hasRestParam && expected > 0) {
                            calleeArgs = args;
                        } else {
                            for (size_t i = 0; i < expected; ++i) {
                                calleeArgs.push_back(i < args.size() ? args[i]
                                                                     : builder_.createConstUndefined());
                            }
                        }
                        // `this` inside the inherited static body is the
                        // RECEIVER class (Derived), not the declaring ancestor.
                        auto recvObjV = lowerExpression(propAccess->expression.get());
                        auto boxedRecvV = boxValueIfNeeded(recvObjV);
                        auto savedThisV2 = builder_.createCall("ts_get_call_this", {}, HIRType::makeAny());
                        builder_.createCall("ts_set_call_this", {boxedRecvV}, HIRType::makeVoid());
                        lastValue_ = builder_.createCall(bmethod->name, calleeArgs, bmethod->returnType);
                        builder_.createCall("ts_set_call_this", {savedThisV2}, HIRType::makeVoid());
                        return;
                    }
                    // Fallback: For imported classes, staticMethods may not be populated
                    // because the class body is compiled later (via module init specialization).
                    // Emit a forward-reference call using the conventional name.
                    {
                        std::string staticFuncName = cls->name + "_static_" + propAccess->name;
                        lastValue_ = builder_.createCall(staticFuncName, args, HIRType::makeAny());
                        return;
                    }
                    break;
                }
            }

            // Case 3b: Extension static method call - Buffer.from(...), Buffer.alloc(...), etc.
            // Check ExtensionRegistry for static methods on extension-defined class types.
            // Only match methods that have a lowering spec (actual runtime function).
            {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::MethodDefinition* extStaticMethod = extReg.findStaticMethod(classNameIdent->name, propAccess->name);
                if (extStaticMethod && extStaticMethod->lowering) {
                    std::string funcName = extStaticMethod->hirName.value_or(extStaticMethod->call);
                    // Map ext.json return type to HIR type for proper downstream handling
                    auto resultType = extTypeRefToHIR(extStaticMethod->returns);
                    lastValue_ = builder_.createCall(funcName, args, resultType);
                    return;
                }
            }

            // Case 4: Node.js builtin module method call - path.basename(...), fs.readFileSync(...), etc.
            // Check against ExtensionRegistry instead of hardcoded list
            auto& registry = ext::ExtensionRegistry::instance();
            if (registry.isRegisteredModule(classNameIdent->name) || registry.isRegisteredObject(classNameIdent->name)) {
                const ext::MethodDefinition* methodDef = registry.findObjectMethod(classNameIdent->name, propAccess->name);

                // If the method is NOT found in the ext.json AND the identifier is a local
                // variable with a known non-module type (string, number, etc.), skip Case 4.
                // This prevents local variables that shadow module names
                // (e.g. `const path = url.fileURLToPath(...)`) from being treated as module calls.
                bool isLocalVarShadow = false;
                if (!methodDef) {
                    // Method not found on the module/object. Could be a local variable
                    // shadowing a module name (e.g., `var events = []` vs `events` module).
                    // Don't generate a bogus ts_{module}_{method} symbol - fall through
                    // to generic method handlers (push, join, etc.) instead.
                    isLocalVarShadow = true;
                }
                // Also check: if the identifier is a locally-declared function (not
                // imported via require), it shadows the extension module. Node.js modules
                // like `assert` are NOT globals — they must be imported via require().
                // A local `function assert(){}` should NOT be treated as the Node assert
                // module. Only check for function-typed locals to avoid shadowing
                // `var path = require('path')` which IS the module.
                // A function PARAMETER named like a Node module shadows the
                // builtin: it holds a user value, not the module. This is the
                // common QUnit/test pattern `function(assert){ assert.deepEqual
                // (...) }` where `assert` is the harness's own object — routing it
                // to the node assert builtin (which exit(1)s on failure) is wrong.
                // A real `var/const path = require('path')` alias is NEVER a
                // parameter, so this does not disturb module aliases. Walk every
                // enclosing function on the scope stack so a CAPTURED parameter
                // (e.g. `assert` used inside a nested `forEach` callback) is also
                // caught, not just a direct parameter of the current function.
                if (!isLocalVarShadow) {
                    for (auto& sc : scopes_) {
                        if (!sc.owningFunction) continue;
                        for (auto& p : sc.owningFunction->params) {
                            if (p.first == classNameIdent->name) { isLocalVarShadow = true; break; }
                        }
                        if (isLocalVarShadow) break;
                    }
                }
                if (!isLocalVarShadow) {
                    auto* varInfo = lookupVariableInfo(classNameIdent->name);
                    // Keep the existing function-typed-local shadow.
                    if (varInfo && varInfo->elemType &&
                        varInfo->elemType->kind == HIRTypeKind::Function) {
                        isLocalVarShadow = true;
                    }
                }

                if (!isLocalVarShadow) {
                // Use the HIR name (matching LoweringRegistry derivation) so the registered lowering spec is found
                std::string runtimeFunc;
                if (methodDef && methodDef->hirName) {
                    runtimeFunc = *methodDef->hirName;
                } else {
                    runtimeFunc = "ts_" + classNameIdent->name + "_" + propAccess->name;
                }
                // Use ext.json return type if available, otherwise default to any
                auto resultType = methodDef ? extTypeRefToHIR(methodDef->returns) : HIRType::makeAny();

                if (methodDef) {
                    // Find if there's a rest parameter and at what position
                    size_t restParamIndex = SIZE_MAX;
                    for (size_t i = 0; i < methodDef->params.size(); ++i) {
                        if (methodDef->params[i].rest) {
                            restParamIndex = i;
                            break;
                        }
                    }

                    // Skip array packing for ALL console functions - they have special
                    // handling in HIRToLLVM (TypeDispatch for log/error/warn/info/debug,
                    // direct single-arg calls for group/time/count/etc.)
                    bool isConsoleFunctionWithSpecialHandling =
                        classNameIdent->name == "console";

                    if (restParamIndex != SIZE_MAX && args.size() >= restParamIndex &&
                        !isConsoleFunctionWithSpecialHandling) {
                        // Pack all arguments from restParamIndex onwards into an array
                        std::vector<std::shared_ptr<HIRValue>> packedArgs;

                        // Copy non-rest arguments
                        for (size_t i = 0; i < restParamIndex; ++i) {
                            packedArgs.push_back(args[i]);
                        }

                        // Create array for rest arguments
                        auto zero = builder_.createConstInt(0);
                        auto restArray = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));

                        // Push rest arguments into the array (boxed)
                        for (size_t i = restParamIndex; i < args.size(); ++i) {
                            auto boxedArg = boxValueIfNeeded(args[i]);
                            builder_.createCall("ts_array_push", {restArray, boxedArg}, HIRType::makeInt64());
                        }

                        packedArgs.push_back(restArray);
                        lastValue_ = builder_.createCall(runtimeFunc, packedArgs, resultType);
                        return;
                    }
                }

                // No rest parameter or not enough args - emit direct call
                lastValue_ = builder_.createCall(runtimeFunc, args, resultType);
                return;
                } // end if (!isLocalVarShadow)
            }
        }

        // Case 4b: Nested object method call - path.posix.join(...), path.win32.basename(...), etc.
        // Pattern: <module>.<nested>.<method>(...)
        {
            auto* innerPropAccess = dynamic_cast<ast::PropertyAccessExpression*>(propAccess->expression.get());
            if (innerPropAccess) {
                auto* moduleIdent = dynamic_cast<ast::Identifier*>(innerPropAccess->expression.get());
                if (moduleIdent) {
                    auto& registry = ext::ExtensionRegistry::instance();
                    const ext::MethodDefinition* methodDef = registry.findNestedObjectMethod(
                        moduleIdent->name, innerPropAccess->name, propAccess->name);
                    if (methodDef && methodDef->lowering) {
                        std::string runtimeFunc;
                        if (methodDef->hirName) {
                            runtimeFunc = *methodDef->hirName;
                        } else {
                            runtimeFunc = "ts_" + moduleIdent->name + "_" + innerPropAccess->name + "_" + propAccess->name;
                        }
                        auto resultType = extTypeRefToHIR(methodDef->returns);

                        // Handle rest parameters (same logic as Case 4)
                        size_t restParamIndex = SIZE_MAX;
                        for (size_t i = 0; i < methodDef->params.size(); ++i) {
                            if (methodDef->params[i].rest) {
                                restParamIndex = i;
                                break;
                            }
                        }
                        if (restParamIndex != SIZE_MAX && args.size() >= restParamIndex) {
                            std::vector<std::shared_ptr<HIRValue>> packedArgs;
                            for (size_t i = 0; i < restParamIndex; ++i) {
                                packedArgs.push_back(args[i]);
                            }
                            auto restArray = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
                            for (size_t i = restParamIndex; i < args.size(); ++i) {
                                auto boxedArg = boxValueIfNeeded(args[i]);
                                builder_.createCall("ts_array_push", {restArray, boxedArg}, HIRType::makeInt64());
                            }
                            packedArgs.push_back(restArray);

                            // Inject platform constant (e.g., 1=win32, 2=posix) for _ex functions
                            if (methodDef->platformArg) {
                                packedArgs.push_back(builder_.createConstInt(*methodDef->platformArg));
                            }

                            lastValue_ = builder_.createCall(runtimeFunc, packedArgs, resultType);
                            return;
                        }

                        // Non-rest args: inject platformArg if present
                        if (methodDef->platformArg) {
                            auto argsWithPlatform = args;
                            argsWithPlatform.push_back(builder_.createConstInt(*methodDef->platformArg));
                            lastValue_ = builder_.createCall(runtimeFunc, argsWithPlatform, resultType);
                            return;
                        }

                        lastValue_ = builder_.createCall(runtimeFunc, args, resultType);
                        return;
                    }
                }
            }
        }

        // Handle Function.prototype.call(thisArg, ...args)
        // Use ts_call_with_this_N to properly save/restore the caller's this context.
        // Previously used ts_set_call_this + ts_call_N which permanently clobbered this.
        if (propAccess->name == "call" && !args.empty()) {
            auto func = lowerExpression(propAccess->expression.get());
            auto boxedFunc = boxValueIfNeeded(func);
            auto boxedThis = boxValueIfNeeded(args[0]);
            // Remaining args (raw — the unified ts_call_with_this lowering boxes
            // them). Routes through createCallWithThis instead of the by-name
            // ts_call_with_this_N family (which had no arity cap → >8 args
            // referenced a nonexistent symbol).
            std::vector<std::shared_ptr<HIRValue>> callArgs(args.begin() + 1, args.end());
            lastValue_ = builder_.createCallWithThis(boxedFunc, boxedThis, callArgs, HIRType::makeAny());
            return;
        }

        // "use fast": a global-builtin method with a typed RuntimeCall
        // resolution (Math.sqrt -> ts_math_sqrt: double) lowers to a DIRECT
        // typed call. The generic CallMethod stamps the result Any, which
        // routes every downstream arithmetic op through the boxed ts_value_*
        // dispatcher, and evaluating the receiver emits a dead
        // ts_get_global_Math() per call — together the dominant cost of the
        // SoA benchmark's inner loop. Gated on fastCode_ (use-fast gating
        // policy); a local binding shadowing the global still wins.
        if (fastCode_) {
            if (auto* gid = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
                if (!lookupVariable(gid->name)) {
                    auto res = BuiltinRegistry::instance().resolveGlobalBuiltin(
                        gid->name, propAccess->name, (int)args.size());
                    if (res.kind == BuiltinResolution::Kind::RuntimeCall &&
                        res.runtimeFunction && res.returnType &&
                        res.returnType->kind != HIRTypeKind::Void &&
                        res.returnType->kind != HIRTypeKind::Any) {
                        lastValue_ = builder_.createCall(res.runtimeFunction,
                                                         args, res.returnType);
                        return;
                    }
                }
            }
        }

        // Fallback: Dynamic method call
        auto obj = lowerExpression(propAccess->expression.get());
        // Private method CALL (`obj.#m()`) on an untyped receiver: brand-check the
        // method access (ts_object_get_private throws if the receiver lacks #m),
        // then invoke it with `this = obj`. A typed receiver is a provable instance
        // and keeps the normal dispatch.
        // ALL private calls brand-check — a typed `this` is NOT provably an
        // instance (c.method.call(foreignObj) rebinds it; the inner-arrow-
        // function family). ts_object_get_private throws on a brand miss.
        if (!propAccess->name.empty() && propAccess->name[0] == '#') {
            auto keyStr = builder_.createConstString(resolvePrivateName(propAccess->name));
            auto boxedObj = boxValueIfNeeded(obj);
            auto func = builder_.createCall("ts_object_get_private",
                {boxedObj, keyStr}, HIRType::makeAny());
            lastValue_ = builder_.createCallWithThis(func, boxedObj, args, HIRType::makeAny());
            return;
        }
        // "use fast": NativeArray.get returns its unboxed element type. The
        // Any default made every `arr.get(j) - x` binary op take the boxed
        // ts_value_* path (3 runtime calls + 2 boxes per op) even though the
        // access itself lowers to an inline load — the other half of the SoA
        // benchmark's 2.3x deficit.
        if (fastCode_ && obj && obj->type &&
            obj->type->kind == HIRTypeKind::Class &&
            obj->type->className == "NativeArray" &&
            (propAccess->name == "get" || propAccess->name == "getUnchecked")) {
            auto elemT = (obj->type->elementType &&
                          obj->type->elementType->kind == HIRTypeKind::Int64)
                             ? HIRType::makeInt64()
                             : HIRType::makeFloat64();
            lastValue_ = builder_.createCallMethod(obj, propAccess->name, args, elemT);
            return;
        }
        lastValue_ = builder_.createCallMethod(obj, resolvePrivateName(propAccess->name), args, HIRType::makeAny());
        return;
    }

    // Handle direct function call
    auto* ident = dynamic_cast<ast::Identifier*>(node->callee.get());
    if (ident) {
        // Synthetic computed-accessor install trigger (injected by the Monomorphizer
        // into __module_init at a top-level class's source position). Runs the
        // computed install now that the variables its keys read are initialized.
        if (ident->name == "__ts_install_computed_accessors") {
            if (!node->arguments.empty()) {
                if (auto* lit = dynamic_cast<ast::StringLiteral*>(node->arguments[0].get())) {
                    HIRClass* hc = nullptr;
                    // The trigger names the class by its source identifier; a class
                    // EXPRESSION (`let C = class …`) registers under a synthetic
                    // `__anon_class_N` name, mapped from the binding by
                    // variableToClassName_. Resolve through it before matching.
                    std::string resolved = lit->value;
                    auto vIt = variableToClassName_.find(lit->value);
                    if (vIt != variableToClassName_.end()) resolved = vIt->second;
                    for (auto& c : module_->classes) if (c && c->name == resolved) { hc = c.get(); break; }
                    if (hc && !hc->computedAccessors.empty()) {
                        auto ctorVal = builder_.createLoadFunction(hc->name + "_constructor");
                        auto proto = builder_.createGetPropStatic(ctorVal, "prototype", HIRType::makeAny());
                        emitComputedAccessorInstalls(hc, proto, ctorVal);
                    }
                }
            }
            lastValue_ = builder_.createConstUndefined();
            return;
        }
        // requires-new: a class constructor invoked WITHOUT `new` throws a
        // TypeError (a class's [[Call]] is non-callable per ECMA-262 — only
        // [[Construct]] is allowed). `new C()` is a NewExpression (handled in
        // visitNewExpression, not here) and `super()` is a SuperExpression, so
        // neither reaches this bare-class-identifier call path.
        for (auto& cls : module_->classes) {
            if (cls && cls->name == ident->name) {
                for (auto& a : node->arguments) lowerExpression(a.get());  // args evaluated for side effects
                auto nameStr = builder_.createConstString("TypeError");
                auto msg = builder_.createConstString(
                    "Class constructor " + ident->name + " cannot be invoked without 'new'");
                auto err = builder_.createCall("ts_error_create_typed_js",
                    {nameStr, msg}, HIRType::makeAny());
                builder_.createThrow(err);
                lastValue_ = builder_.createConstUndefined();
                return;
            }
        }
        // GC verification-harness builtins (GC-001). Handled FIRST so the
        // analyzer's FunctionType registration can't divert them to a weak
        // undefined-returning stub. Drive/inspect the collector from compiled
        // TS so a single allocation + forced GC reproduces moving-GC corruption.
        if (ident->name == "__ts_gc_minor") {
            lastValue_ = builder_.createCall("ts_gc_minor_collect", {}, HIRType::makeVoid());
            return;
        }
        if (ident->name == "__ts_gc_major") {
            lastValue_ = builder_.createCall("ts_gc_force_collect", {}, HIRType::makeVoid());
            return;
        }
        if (ident->name == "__ts_gc_collection_count") {
            lastValue_ = builder_.createCall("ts_gc_dbg_collection_count", {}, HIRType::makeFloat64());
            return;
        }
        if (ident->name == "__ts_gc_live_size") {
            lastValue_ = builder_.createCall("ts_gc_dbg_live_size", {}, HIRType::makeFloat64());
            return;
        }
        if (ident->name == "__ts_gc_verify") {
            // Runs a verified minor GC; returns the number of invariant violations.
            lastValue_ = builder_.createCall("ts_gc_verify_now", {}, HIRType::makeFloat64());
            return;
        }
        if (ident->name == "__ts_gc_is_nursery") {
            if (args.empty()) { lastValue_ = builder_.createConstBool(false); return; }
            // Box the argument to a TsValue* so the runtime can unbox uniformly.
            auto arg = args[0];
            std::shared_ptr<HIRValue> boxed;
            if (arg->type) {
                switch (arg->type->kind) {
                    case HIRTypeKind::Int64:  boxed = builder_.createBoxInt(arg); break;
                    case HIRTypeKind::Float64: boxed = builder_.createBoxFloat(arg); break;
                    case HIRTypeKind::Bool:   boxed = builder_.createBoxBool(arg); break;
                    case HIRTypeKind::String: boxed = builder_.createBoxString(arg); break;
                    case HIRTypeKind::Any:    boxed = arg; break;
                    default:                  boxed = builder_.createBoxObject(arg); break;
                }
            } else {
                boxed = builder_.createBoxObject(arg);
            }
            lastValue_ = builder_.createCall("ts_gc_dbg_is_nursery", {boxed}, HIRType::makeBool());
            return;
        }
        if (ident->name == "__ts_gc_watch") {
            if (args.empty()) { lastValue_ = builder_.createConstBool(false); return; }
            auto arg = args[0];
            std::shared_ptr<HIRValue> boxed;
            if (arg->type) {
                switch (arg->type->kind) {
                    case HIRTypeKind::Int64:  boxed = builder_.createBoxInt(arg); break;
                    case HIRTypeKind::Float64: boxed = builder_.createBoxFloat(arg); break;
                    case HIRTypeKind::Bool:   boxed = builder_.createBoxBool(arg); break;
                    case HIRTypeKind::String: boxed = builder_.createBoxString(arg); break;
                    case HIRTypeKind::Any:    boxed = arg; break;
                    default:                  boxed = builder_.createBoxObject(arg); break;
                }
            } else {
                boxed = builder_.createBoxObject(arg);
            }
            lastValue_ = builder_.createCall("ts_gc_dbg_watch", {boxed}, HIRType::makeVoid());
            return;
        }
        if (ident->name == "__ts_gc_watch_alive") {
            lastValue_ = builder_.createCall("ts_gc_dbg_watch_alive", {}, HIRType::makeBool());
            return;
        }
        if (ident->name == "__ts_dbg_bits") {
            if (args.empty()) { lastValue_ = builder_.createConstBool(false); return; }
            auto arg = args[0];
            std::shared_ptr<HIRValue> boxed;
            if (arg->type) {
                switch (arg->type->kind) {
                    case HIRTypeKind::Int64:  boxed = builder_.createBoxInt(arg); break;
                    case HIRTypeKind::Float64: boxed = builder_.createBoxFloat(arg); break;
                    case HIRTypeKind::Bool:   boxed = builder_.createBoxBool(arg); break;
                    case HIRTypeKind::String: boxed = builder_.createBoxString(arg); break;
                    case HIRTypeKind::Any:    boxed = arg; break;
                    default:                  boxed = builder_.createBoxObject(arg); break;
                }
            } else {
                boxed = builder_.createBoxObject(arg);
            }
            lastValue_ = builder_.createCall("ts_dbg_bits", {boxed}, HIRType::makeVoid());
            return;
        }

        // First check if this is a captured variable from an outer function
        size_t scopeIndex = 0;
        if (currentFunction_ && isCapturedVariable(ident->name, &scopeIndex)) {
            // For module-level variables that have __modvar_ globals, prefer
            // the global over closure cells. Closure cells may be null due to
            // capture ordering (e.g., fmtLong captures plural, but plural's
            // closure isn't created yet when fmtLong's closure is created).
            if (isModuleGlobalVar(ident->name)) {
                std::string globalName = modVarName(ident->name);
                auto funcPtr = builder_.createLoadGlobalTyped(globalName, HIRType::makeAny());
                lastValue_ = builder_.createCallIndirect(funcPtr, args, HIRType::makeAny());
                return;
            }
            // Look up the variable info to get its type
            auto* info = lookupVariableInfo(ident->name);
            if (info) {
                auto type = info->elemType ? info->elemType : (info->value ? info->value->type : HIRType::makeAny());
                // Register this capture for the current function
                registerCapture(ident->name, type, scopeIndex);
                // Mark the function as having closures
                currentFunction_->hasClosure = true;
                // Use LoadCapture for captured variables
                auto funcPtr = builder_.createLoadCapture(ident->name, type);
                // Get return type from function type if available
                std::shared_ptr<HIRType> resultType = HIRType::makeAny();
                if (type && type->kind == HIRTypeKind::Function && type->returnType) {
                    resultType = type->returnType;
                }
                lastValue_ = builder_.createCallIndirect(funcPtr, args, resultType);
                return;
            }
        }

        // Mirror visitIdentifier's module-global rule: inside __module_init_*,
        // a module-level var that inner functions WRITE must be CALLED through
        // the __modvar_ global — the local alloca is a stale snapshot. The
        // classic victim: `var resolve; new Promise(r => resolve = r);
        // resolve(42)` silently called undefined (deferred-Promise pattern,
        // ~60 test262 Promise tests).
        if (isModuleGlobalVar(ident->name) && isModuleGlobalUsedByInner(ident->name) &&
            currentFunction_ &&
            currentFunction_->name.find("__module_init_") == 0) {
            std::string globalName = modVarName(ident->name);
            auto funcPtr = builder_.createLoadGlobalTyped(globalName, HIRType::makeAny());
            lastValue_ = builder_.createCallIndirect(funcPtr, args, HIRType::makeAny());
            return;
        }

        // Check if this is a local variable (might be a closure)
        auto* info = lookupVariableInfo(ident->name);
        if (info) {
            // It's a local variable - load the function pointer and call indirectly
            std::shared_ptr<HIRValue> funcPtr;
            std::shared_ptr<HIRType> funcType;
            if (info->isAlloca && info->elemType) {
                funcPtr = builder_.createLoad(info->elemType, info->value);
                funcType = info->elemType;
            } else {
                funcPtr = info->value;
                funcType = info->value->type;
            }
            // Get return type from function type if available
            std::shared_ptr<HIRType> resultType = HIRType::makeAny();
            if (funcType && funcType->kind == HIRTypeKind::Function && funcType->returnType) {
                resultType = funcType->returnType;
            }
            lastValue_ = builder_.createCallIndirect(funcPtr, args, resultType);
            return;
        }
        // Check if this is a CJS module binding (stored in __modvar_ global).
        // CJS named imports that are function expressions (not FunctionDeclarations)
        // are stored in moduleGlobalVars_ and must be called indirectly.
        if (isModuleGlobalVar(ident->name)) {
            std::string globalName = modVarName(ident->name);
            auto funcPtr = builder_.createLoadGlobalTyped(globalName, HIRType::makeAny());
            lastValue_ = builder_.createCallIndirect(funcPtr, args, HIRType::makeAny());
            return;
        }
        // Handle builtin globals that are called as functions
        if (ident->name == "Symbol") {
            // Symbol(description?) creates a unique symbol
            std::shared_ptr<HIRValue> desc;
            if (!args.empty()) {
                desc = args[0];
            } else {
                desc = builder_.createConstNull();
            }
            lastValue_ = builder_.createCall("ts_symbol_create", {desc}, HIRType::makeSymbol());
            return;
        }

        if (ident->name == "BigInt") {
            // BigInt(value) converts value to BigInt
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_bigint_from_value", {args[0]}, HIRType::makeBigInt());
            } else {
                lastValue_ = builder_.createConstNull();
            }
            return;
        }

        if (ident->name == "Boolean") {
            // Boolean(value) converts to boolean using JavaScript truthiness
            if (!args.empty()) {
                // ts_value_to_bool expects a boxed TsValue*, so we need to box the argument
                auto arg = args[0];
                std::shared_ptr<HIRValue> boxed;
                if (arg->type) {
                    switch (arg->type->kind) {
                        case HIRTypeKind::Int64:
                            boxed = builder_.createBoxInt(arg);
                            break;
                        case HIRTypeKind::Float64:
                            boxed = builder_.createBoxFloat(arg);
                            break;
                        case HIRTypeKind::Bool:
                            boxed = builder_.createBoxBool(arg);
                            break;
                        case HIRTypeKind::String:
                            boxed = builder_.createBoxString(arg);
                            break;
                        case HIRTypeKind::Any:
                            // Already boxed
                            boxed = arg;
                            break;
                        default:
                            // For objects, arrays, etc. - box as object
                            boxed = builder_.createBoxObject(arg);
                            break;
                    }
                } else {
                    // Unknown type, assume it needs boxing as object
                    boxed = builder_.createBoxObject(arg);
                }
                lastValue_ = builder_.createCall("ts_value_to_bool", {boxed}, HIRType::makeBool());
            } else {
                lastValue_ = builder_.createConstBool(false);
            }
            return;
        }

        if (ident->name == "gc") {
            // gc() forces garbage collection
            lastValue_ = builder_.createCall("ts_gc_collect", {}, HIRType::makeVoid());
            return;
        }

        if (ident->name == "Number") {
            // Number(value) converts to number
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_to_number", {args[0]}, HIRType::makeFloat64());
            } else {
                lastValue_ = builder_.createConstFloat(0.0);
            }
            return;
        }

        if (ident->name == "String") {
            // String(value) converts to string. Use ts_string_ctor (not
            // ts_to_string) so a Symbol arg yields SymbolDescriptiveString
            // ("Symbol(desc)") instead of throwing — ECMA-262 22.1.1.1.
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_string_ctor", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createCall("ts_string_create", {builder_.createConstNull()}, HIRType::makeString());
            }
            return;
        }

        if (ident->name == "Array") {
            // Array() → empty array; Array(n) → sized array; Array(a,b,c) → [a,b,c]
            if (args.empty()) {
                lastValue_ = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
            } else if (args.size() == 1) {
                lastValue_ = builder_.createCall("ts_array_constructor", {args[0]}, HIRType::makeArray(HIRType::makeAny(), false));
            } else {
                // Array(a, b, c) → create + push each element
                auto arr = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
                for (auto& arg : args) {
                    builder_.createCall("ts_array_push_any", {arr, arg}, HIRType::makeVoid());
                }
                lastValue_ = arr;
            }
            return;
        }

        if (ident->name == "Object") {
            // Object() and Object(value) - create or return object
            if (args.empty()) {
                lastValue_ = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());
            } else {
                lastValue_ = builder_.createCall("ts_object_constructor", {args[0]}, HIRType::makeAny());
            }
            return;
        }

        if (ident->name == "eval") {
            // eval(x) — EVAL-001: route through the tree-walking interpreter
            // with an explicitly BOXED argument. The generic untyped path
            // passes number literals as raw doubles into the varargs @eval
            // symbol, which the nanboxed runtime side cannot decode.
            std::shared_ptr<HIRValue> evalArg = args.empty()
                ? builder_.createConstUndefined()
                : boxValueIfNeeded(args[0]);
            // Direct eval inside a PARAMETER INITIALIZER: pass context flags so
            // the interpreter applies ES2025 19.2.1.3 step 5.d (sloppy direct
            // eval declaring 'arguments' while an arguments binding lies on the
            // lexEnv->varEnv walk => SyntaxError) and keeps eval var
            // declarations out of globalThis (the eval's varEnv is the function
            // env, never the global). Owner check keeps nested function bodies
            // lowered inside a default expression on the plain path.
            if (activeEvalFlags_ != 0 && evalFlagsOwner_ == currentFunction_) {
                lastValue_ = builder_.createCall(
                    "ts_direct_eval_value",
                    {evalArg, builder_.createConstInt(activeEvalFlags_)},
                    HIRType::makeAny());
                return;
            }
            // STRICT caller: direct eval's var declarations go to eval's OWN
            // environment, never the global (ES 19.2.1.3 step 10 strictEval),
            // and the eval'd code inherits strictness. flags bit2.
            if (strictCode_) {
                lastValue_ = builder_.createCall(
                    "ts_direct_eval_value",
                    {evalArg, builder_.createConstInt(4)},
                    HIRType::makeAny());
                return;
            }
            lastValue_ = builder_.createCall("ts_indirect_eval_value",
                                             {evalArg}, HIRType::makeAny());
            return;
        }

        if (ident->name == "Function") {
            // Function(p1, ..., pn, body) — EVAL-001: pass ALL arguments so
            // the runtime can assemble "(function anonymous(p1,...,pn){body})"
            // for the tree-walking interpreter (params were previously
            // dropped; body = LAST argument per 20.2.1.1).
            auto fcArgs = builder_.createCall(
                "ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
            for (auto& a : args) {
                auto boxed = boxValueIfNeeded(a);
                builder_.createCall("ts_array_push", {fcArgs, boxed},
                                    HIRType::makeInt64());
            }
            lastValue_ = builder_.createCall("ts_function_constructor_args",
                                             {fcArgs}, HIRType::makeAny());
            return;
        }

        // Date(...) without `new`: per ECMA-262 21.4.2.1, returns the
        // current time as a string regardless of args. The args are
        // evaluated for side effects but discarded.
        if (ident->name == "Date") {
            // Evaluate args for side-effect, then call ts_date_now_string().
            // (createCall with the evaluated arg values is unnecessary —
            // they were already evaluated when args was built.)
            lastValue_ = builder_.createCall("ts_date_now_string", {}, HIRType::makeString());
            return;
        }

        // RegExp(pattern[, flags]) — same semantics as `new RegExp(...)` per
        // ECMA-262 §22.2.4.1 RegExp Constructor. Without this case, the call
        // fell through to user-function resolution and the Monomorphizer
        // generated an empty-body stub (RegExp_m<hash>_any → undefined),
        // which silently broke libraries like lodash that do
        // `var re = RegExp('...')` at module init.
        if (ident->name == "RegExp") {
            std::shared_ptr<HIRValue> patternArg;
            std::shared_ptr<HIRValue> flagsArg;
            if (!node->arguments.empty()) {
                patternArg = lowerExpression(node->arguments[0].get());
            } else {
                patternArg = builder_.createConstString("");
            }
            if (node->arguments.size() >= 2) {
                flagsArg = lowerExpression(node->arguments[1].get());
            } else {
                flagsArg = builder_.createConstNull();
            }
            lastValue_ = builder_.createCall("ts_regexp_create",
                {patternArg, flagsArg}, HIRType::makeObject());
            return;
        }

        // Error constructors called as functions (without new) - same as new Error()
        if (ident->name == "Error" || ident->name == "TypeError" || ident->name == "RangeError" ||
            ident->name == "ReferenceError" || ident->name == "SyntaxError" || ident->name == "URIError" ||
            ident->name == "EvalError") {
            std::shared_ptr<HIRValue> message;
            if (!args.empty()) {
                message = args[0];
            } else {
                message = builder_.createConstString("");
            }
            if (ident->name != "Error") {
                auto nameStr = builder_.createConstString(ident->name);
                lastValue_ = builder_.createCall("ts_error_create_typed_js", {nameStr, message}, HIRType::makeAny());
            } else {
                lastValue_ = builder_.createCall("ts_error_create", {message}, HIRType::makeAny());
            }
            return;
        }

        // Global URI encoding/decoding functions
        if (ident->name == "encodeURIComponent") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_encode_uri_component", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "decodeURIComponent") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_decode_uri_component", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "encodeURI") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_encode_uri", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "decodeURI") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_decode_uri", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "escape") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_escape", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }
        if (ident->name == "unescape") {
            if (!args.empty()) {
                lastValue_ = builder_.createCall("ts_unescape", {args[0]}, HIRType::makeString());
            } else {
                lastValue_ = builder_.createConstString("undefined");
            }
            return;
        }

        // Not a local variable - direct function call
        // First check specializations for rest parameters - this info is available
        // even before the HIR functions are created
        HIRFunction* targetFunc = nullptr;
        std::string callName;
        bool hasRestParam = false;
        size_t restParamIndex = 0;
        std::shared_ptr<HIRType> restElemType = HIRType::makeAny();

        // Track if we found default parameters and should use the specialization's name
        bool hasDefaultParams = false;
        size_t requiredParamCount = 0;
        size_t totalParamCount = 0;
        ast::FunctionDeclaration* foundFuncNode = nullptr;

        // Look up specialization by original function name to check for rest params and default params
        if (specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == ident->name) {
                    // Found a specialization for this function
                    if (auto* funcNode = dynamic_cast<ast::FunctionDeclaration*>(spec.node)) {
                        foundFuncNode = funcNode;
                        totalParamCount = funcNode->parameters.size();

                        // Check if function has rest parameter or default parameters
                        for (size_t i = 0; i < funcNode->parameters.size(); ++i) {
                            if (funcNode->parameters[i]->isRest) {
                                hasRestParam = true;
                                restParamIndex = i;
                                // Get the element type from the parameter type annotation
                                // e.g., "...numbers: number[]" -> element type is number (Float64)
                                std::string paramType = funcNode->parameters[i]->type;
                                if (!paramType.empty()) {
                                    // Extract element type from array type (e.g., "number[]" -> "number")
                                    if (paramType.size() > 2 && paramType.substr(paramType.size() - 2) == "[]") {
                                        std::string elemTypeStr = paramType.substr(0, paramType.size() - 2);
                                        restElemType = convertTypeFromString(elemTypeStr);
                                    }
                                }
                                // Use this specialization's name - it already has the correct mangling
                                callName = spec.specializedName;
                                break;
                            }
                            // Check for default parameter
                            if (funcNode->parameters[i]->initializer) {
                                hasDefaultParams = true;
                            } else {
                                // Count required params (params before any with defaults)
                                if (!hasDefaultParams) {
                                    requiredParamCount = i + 1;
                                }
                            }
                        }

                        // If function has default params, always use the specialization name
                        // because params with defaults are now Any type
                        if (!hasRestParam && hasDefaultParams) {
                            callName = spec.specializedName;
                            // Look up the HIR function to get param types for boxing
                            for (auto& f : module_->functions) {
                                if (f->name == spec.specializedName) {
                                    targetFunc = f.get();
                                    break;
                                }
                            }
                            // Pad args with undefined for missing default params
                            if (args.size() < totalParamCount) {
                                for (size_t i = args.size(); i < totalParamCount; ++i) {
                                    args.push_back(builder_.createConstUndefined());
                                }
                            }
                        }
                    }
                    if (hasRestParam || (hasDefaultParams && !callName.empty())) break;
                }
            }
        }

        // If we didn't find a rest-parameter function or function with default params,
        // compute the mangled name based on argument types
        if (!hasRestParam && callName.empty()) {
            std::vector<std::shared_ptr<ts::Type>> argTypes;
            for (auto& arg : node->arguments) {
                argTypes.push_back(arg->inferredType ? arg->inferredType : std::make_shared<ts::Type>(ts::TypeKind::Any));
            }
            std::string mangledName = Monomorphizer::generateMangledName(ident->name, argTypes, node->resolvedTypeArguments, currentModulePath_);
            callName = mangledName;

            // Look up the function - try mangled name first, then original name
            for (auto& f : module_->functions) {
                if (f->name == mangledName) {
                    targetFunc = f.get();
                    break;
                }
            }
            // If not found with mangled name, try original name (for runtime functions etc.)
            if (!targetFunc) {
                for (auto& f : module_->functions) {
                    if (f->name == ident->name) {
                        targetFunc = f.get();
                        callName = ident->name;  // Use original name
                        break;
                    }
                }
            }
            // Counter-form fallback: visitFunctionDeclaration (line ~2044)
            // appends `_<counter>` to nested function declarations, producing
            // names like `verifyProperty_3`. The call site computed the
            // type-mangled name (`verifyProperty_any_any_any`) which doesn't
            // match. If neither lookup found a target, scan for an exact
            // base-name match against names of the form `<name>_<digits>$`
            // and use that.
            //
            // EXCLUSION: skip this fallback when ident->name is a known
            // global builtin (isFinite, parseInt, etc.). Bundled JS modules
            // like lodash declare inner functions with these same names
            // inside their IIFE wrappers (`function isFinite(value)` inside
            // `runInContext`). The greedy scan would grab the inner function
            // by base-name + `_<digits>` pattern and shadow the global.
            // The known-globals branch below (lines ~7029-7062) handles the
            // correct lookup against the runtime registration.
            auto isBareGlobalIdent = [](const std::string& n) {
                return n == "isFinite" || n == "isNaN" ||
                       n == "parseInt" || n == "parseFloat" ||
                       n == "encodeURI" || n == "encodeURIComponent" ||
                       n == "decodeURI" || n == "decodeURIComponent" ||
                       n == "eval";
            };
            if (!targetFunc && !isBareGlobalIdent(ident->name)) {
                for (auto& f : module_->functions) {
                    const std::string& fn = f->name;
                    if (fn.size() <= ident->name.size() + 1) continue;
                    if (fn.compare(0, ident->name.size(), ident->name) != 0) continue;
                    if (fn[ident->name.size()] != '_') continue;
                    bool allDigits = true;
                    for (size_t i = ident->name.size() + 1; i < fn.size(); ++i) {
                        if (fn[i] < '0' || fn[i] > '9') { allDigits = false; break; }
                    }
                    if (allDigits) {
                        targetFunc = f.get();
                        callName = fn;
                        break;
                    }
                }
            }

            // Imported user-module function: its specializations are emitted
            // under the CALLEE module's hash (`f_m<hash>_<types>`), but
            // mangledName above used the CALLER's module path, so both
            // lookups missed and the call would fall through to a weak
            // undefined-returning stub (silent cross-module breakage —
            // capitalize()/sumSquares() returned undefined/NaN). Retry
            // tolerant of the module hash: match specializations of the form
            // `<exported>_m<digits><typeSuffix>` where typeSuffix is the same
            // arg-type mangling this call site computed. When two modules
            // export the same name, prefer the one whose source file matches
            // the import specifier's basename.
            if (!targetFunc && specializations_) {
                // Look up the import binding in the CALL's own file (falls
                // back to any file that imports this name — covers nodes
                // with an empty sourceFile).
                const std::pair<std::string, std::string>* imp = nullptr;
                auto fileIt = userModuleImports_.find(node->sourceFile);
                if (fileIt != userModuleImports_.end()) {
                    auto it2 = fileIt->second.find(ident->name);
                    if (it2 != fileIt->second.end()) imp = &it2->second;
                }
                if (!imp && node->sourceFile.empty()) {
                    for (auto& [file, m] : userModuleImports_) {
                        auto it2 = m.find(ident->name);
                        if (it2 != m.end()) { imp = &it2->second; break; }
                    }
                }
                if (imp) {
                    auto baseName = [](std::string s) {
                        auto sl = s.find_last_of("/\\");
                        if (sl != std::string::npos) s = s.substr(sl + 1);
                        auto dot = s.rfind('.');
                        if (dot != std::string::npos) s = s.substr(0, dot);
                        return s;
                    };
                    std::string wantBase = baseName(imp->first);
                    // Candidate binding names: the exported name, and (for
                    // aliased imports) the LOCAL name — the monomorphizer
                    // creates the specialization under the usage (local)
                    // name when it resolves the alias via findFunction.
                    std::vector<std::string> candidates = { imp->second };
                    if (ident->name != imp->second) candidates.push_back(ident->name);
                    std::string resolvedName;
                    for (const auto& cand : candidates) {
                        if (!resolvedName.empty()) break;
                        std::string bare = Monomorphizer::generateMangledName(
                            cand, argTypes, node->resolvedTypeArguments, "");
                        std::string typeSuffix = bare.substr(cand.size());
                        auto moduleMangled = [&](const std::string& fn,
                                                 bool requireSuffix) -> bool {
                            if (fn.size() <= cand.size() + 2) return false;
                            if (fn.compare(0, cand.size(), cand) != 0) return false;
                            size_t p = cand.size();
                            if (fn.compare(p, 2, "_m") != 0) return false;
                            p += 2;
                            size_t d = p;
                            while (d < fn.size() && fn[d] >= '0' && fn[d] <= '9') ++d;
                            if (d == p) return false;
                            if (requireSuffix)
                                return fn.compare(d, std::string::npos, typeSuffix) == 0;
                            return d == fn.size() || fn[d] == '_';
                        };
                        // Two passes: exact type-suffix first (the
                        // monomorphizer created that variant from this very
                        // call site), then any variant with matching arity.
                        for (int pass = 0; pass < 2 && resolvedName.empty(); ++pass) {
                            const Specialization* pick = nullptr;
                            for (const auto& spec : *specializations_) {
                                if (spec.originalName != cand) continue;
                                if (!moduleMangled(spec.specializedName, pass == 0)) continue;
                                if (pass == 1) {
                                    auto* fd = dynamic_cast<ast::FunctionDeclaration*>(spec.node);
                                    if (!fd || fd->parameters.size() != args.size()) continue;
                                }
                                bool baseMatch = spec.node &&
                                    baseName(spec.node->sourceFile) == wantBase;
                                if (baseMatch) { pick = &spec; break; }
                                if (!pick) pick = &spec;
                            }
                            if (pick) resolvedName = pick->specializedName;
                        }
                    }
                    if (!resolvedName.empty()) {
                        callName = resolvedName;
                        for (auto& f : module_->functions) {
                            if (f->name == callName) { targetFunc = f.get(); break; }
                        }
                    } else {
                        SPDLOG_WARN("[IMPORT] unresolved imported call '{}' from '{}'"
                                    " — no matching specialization; will stub",
                                    ident->name, imp->first);
                    }
                }
            }
        }
        // If still not found, determine if this is a runtime function or user function
        if (!targetFunc) {
            // Check if this is a named import from an extension module
            // e.g., import { join } from 'path'; join('a', 'b')
            auto extIt = extensionImports_.find(ident->name);
            if (extIt != extensionImports_.end()) {
                const auto& [moduleName, exportedName] = extIt->second;
                auto& extReg2 = ext::ExtensionRegistry::instance();
                const ext::MethodDefinition* methodDef = extReg2.findObjectMethod(moduleName, exportedName);

                std::string runtimeFunc;
                if (methodDef && methodDef->hirName) {
                    runtimeFunc = *methodDef->hirName;
                } else {
                    runtimeFunc = "ts_" + moduleName + "_" + exportedName;
                }
                auto resultType = methodDef ? extTypeRefToHIR(methodDef->returns) : HIRType::makeAny();

                // Handle rest parameters (same logic as Case 4)
                if (methodDef) {
                    size_t restParamIndex = SIZE_MAX;
                    for (size_t i = 0; i < methodDef->params.size(); ++i) {
                        if (methodDef->params[i].rest) {
                            restParamIndex = i;
                            break;
                        }
                    }

                    if (restParamIndex != SIZE_MAX && args.size() >= restParamIndex) {
                        std::vector<std::shared_ptr<HIRValue>> packedArgs;
                        for (size_t i = 0; i < restParamIndex; ++i) {
                            packedArgs.push_back(args[i]);
                        }
                        auto restArray = builder_.createCall("ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
                        for (size_t i = restParamIndex; i < args.size(); ++i) {
                            auto boxedArg = boxValueIfNeeded(args[i]);
                            builder_.createCall("ts_array_push", {restArray, boxedArg}, HIRType::makeInt64());
                        }
                        packedArgs.push_back(restArray);
                        lastValue_ = builder_.createCall(runtimeFunc, packedArgs, resultType);
                        return;
                    }
                }

                lastValue_ = builder_.createCall(runtimeFunc, args, resultType);
                return;
            }
            // Check ExtensionRegistry: if this is a registered module/object being called
            // directly (e.g., assert(true)), use its "default" method
            auto& extReg = ext::ExtensionRegistry::instance();
            if (extReg.isRegisteredModule(ident->name) || extReg.isRegisteredObject(ident->name)) {
                const ext::MethodDefinition* defaultMethod = extReg.findObjectMethod(ident->name, "default");
                if (defaultMethod) {
                    callName = defaultMethod->hirName.value_or(defaultMethod->call);
                } else {
                    callName = ident->name;  // Keep original name for registered modules
                }
            }
            // Runtime functions start with "ts_" - use original name
            // User functions should use the mangled name
            else if (ident->name.substr(0, 3) == "ts_" ||
                ident->name == "console" ||
                ident->name == "Math" ||
                ident->name == "JSON" ||
                ident->name == "parseInt" ||
                ident->name == "isNaN" ||
                ident->name == "isFinite" ||
                ident->name == "eval" ||
                ident->name == "isProxy" ||
                ident->name == "assertThrowsInstanceOf" ||
                ident->name == "assertThrowsValue" ||
                ident->name == "raisesException" ||
                ident->name == "assertDeepEq" ||
                ident->name == "serialize" ||
                ident->name == "deserialize" ||
                ident->name == "testLenientAndStrict" ||
                ident->name == "createNewGlobal" ||
                ident->name == "getTimeZone" ||
                ident->name == "hasProp" ||
                ident->name == "disassemble" ||
                ident->name == "returns" ||
                ident->name == "assertThrowsInstanceOfWithMessage" ||
                ident->name == "assertThrowsInstanceOfWithMessageContains" ||
                ident->name == "completesNormally" ||
                ident->name == "Permutations" ||
                ident->name == "makeIterator" ||
                ident->name == "setTimeZone" ||
                ident->name == "setDefaultLocale" ||
                ident->name == "parseRaisesException" ||
                ident->name == "parsesSuccessfully" ||
                ident->name == "fetch" ||
                ident->name == "require") {
                callName = ident->name;  // Keep original name for runtime functions
            }
            else if (ident->name == "parseFloat") {
                // The bare `parseFloat` symbol mis-binds in the shared-runtime
                // link (returned undefined for every input); use the alias.
                callName = "ts_global_parseFloat";
            }
            // Otherwise keep the mangled name (already set above)
        }

        // Handle rest parameters: package excess arguments into an array
        // We use the hasRestParam flag computed from specializations_ lookup above
        if (hasRestParam) {
            std::vector<std::shared_ptr<HIRValue>> newArgs;

            // Add arguments before the rest parameter
            for (size_t i = 0; i < restParamIndex && i < args.size(); ++i) {
                newArgs.push_back(args[i]);
            }

            // Pad with undefined for missing non-rest arguments
            while (newArgs.size() < restParamIndex) {
                newArgs.push_back(builder_.createConstUndefined());
            }

            // Create the rest array
            size_t restArgsCount = (args.size() > restParamIndex) ? args.size() - restParamIndex : 0;
            auto lenVal = builder_.createConstInt(static_cast<int64_t>(restArgsCount));
            auto restArray = builder_.createNewArrayBoxed(lenVal, restElemType);

            // Add elements to the rest array
            for (size_t i = restParamIndex; i < args.size(); ++i) {
                auto idxVal = builder_.createConstInt(static_cast<int64_t>(i - restParamIndex));
                builder_.createSetElem(restArray, idxVal, args[i]);
            }

            newArgs.push_back(restArray);
            args = std::move(newArgs);
        } else if (targetFunc) {
            // Match args to declared params: pad short with undefined, truncate
            // long. The LLVM verifier rejects either mismatch on direct calls.
            if (args.size() < targetFunc->params.size()) {
                for (size_t i = args.size(); i < targetFunc->params.size(); ++i) {
                    args.push_back(builder_.createConstUndefined());
                }
            } else if (args.size() > targetFunc->params.size()) {
                args.resize(targetFunc->params.size());
            }
        }

        // Box arguments when target parameter is Any type but argument has concrete type
        if (targetFunc) {
            for (size_t i = 0; i < args.size() && i < targetFunc->params.size(); ++i) {
                const auto& [paramName, paramType] = targetFunc->params[i];
                if (paramType && paramType->kind == HIRTypeKind::Any) {
                    // Parameter is Any, need to box the argument if it has a concrete type
                    args[i] = boxValueIfNeeded(args[i]);
                }
            }
        }

        // For require() calls, inject the referrer path as the second argument
        // so the runtime can resolve relative paths correctly.
        // ts_require(TsValue* specifier, const char* referrerPath)
        if (callName == "require") {
            std::string referrerPath = node->sourceFile;
            if (referrerPath.empty()) {
                referrerPath = mainSourceFile_;
            }
            auto referrerVal = builder_.createConstCString(referrerPath);
            args.push_back(referrerVal);
        }

        // Set ts_last_call_argc before direct calls so the 'arguments' object
        // (if the callee creates one) knows how many args were actually passed.
        {
            auto actualArgc = builder_.createConstInt(static_cast<int64_t>(node->arguments.size()));
            builder_.createCall("ts_set_last_call_argc", {actualArgc}, HIRType::makeVoid());
        }

        // OrdinaryCallBindThis for PLAIN calls (ECMA-262 10.2.1.2): f() with
        // no receiver runs the callee with this = undefined. The dynamic
        // call-this slot otherwise leaks the enclosing receiver or the
        // startup globalThis seed into the callee, which a STRICT callee
        // observes directly (test262 language/function-code/10.4.3-1-*).
        // Sloppy callees still see globalThis via ts_this_coerce_sloppy at
        // their this-read. Save/restore keeps the outer receiver intact for
        // `this` reads after the call. Direct NAMED calls can never target
        // an arrow function (arrows read lexical `this` from this same
        // slot), so this site is arrow-safe; the indirect-call path must
        // stay untouched until arrows capture `this` lexically.
        // SKIP for internal/synthetic callees: runtime ts_* helpers never
        // read `this`, and the Monomorphizer-generated user_main invokes
        // __module_init_* through this site — module init IS toplevel code,
        // whose `this` must stay globalThis (script semantics), not the
        // bound undefined (strict programs printed `this` === undefined).
        bool internalCallee =
            callName.rfind("ts_", 0) == 0 ||
            callName.rfind("__module_init", 0) == 0 ||
            callName.rfind("__ts_", 0) == 0;
        std::shared_ptr<HIRValue> savedPlainThis;
        if (!internalCallee) {
            savedPlainThis = builder_.createCall("ts_this_begin_plain_call",
                                                 {}, HIRType::makeAny());
        }
        // Determine return type from target function if available
        auto returnType = (targetFunc && targetFunc->returnType) ? targetFunc->returnType : HIRType::makeAny();
        // "use fast": a direct call to a user function carries its ANNOTATED
        // return type. targetFunc was previously resolved only for
        // default-param calls — every other direct call degraded to Any,
        // which re-boxed downstream arithmetic and lost NativeArray element
        // types across function boundaries (probe tmp/p8_min2.ts). The
        // annotation string converts order-independently (the callee's spec
        // may not be generated yet). Async/generators return Promise/
        // Generator, not the annotation — FastCheck bans them in fast files,
        // but guard anyway.
        if (fastCode_ && returnType->kind == HIRTypeKind::Any && foundFuncNode &&
            !foundFuncNode->returnType.empty() &&
            !foundFuncNode->isAsync && !foundFuncNode->isGenerator) {
            returnType = convertTypeFromString(foundFuncNode->returnType);
        }
        lastValue_ = builder_.createCall(callName, args, returnType);
        if (!internalCallee) {
            builder_.createCall("ts_set_call_this", {savedPlainThis},
                                HIRType::makeVoid());
        }
        return;
    }

    // Computed method call: obj[key](args). The receiver MUST be bound as
    // `this` — the generic indirect call below would call obj[key] with no
    // receiver (e.g. `o['bump']()` ran with this=undefined → NaN; lodash
    // `getMapData(...)['delete'](key)` silently no-op'd). Mirror the dot-method
    // path but with a dynamic key: fetch obj[key], then invoke via
    // ts_call_with_this_N(func, obj, ...args). (Static obj.method() is handled
    // by the PropertyAccess block above.)
    if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(node->callee.get())) {
        auto obj = lowerExpression(ea->expression.get());
        auto boxedObj = boxValueIfNeeded(obj);
        auto keyVal = lowerExpression(ea->argumentExpression.get());
        auto func = builder_.createCall("ts_object_get_dynamic",
            {boxedObj, boxValueIfNeeded(keyVal)}, HIRType::makeAny());
        // obj[key](...) binds obj as `this` for all arities. Routes through the
        // unified ts_call_with_this; previously >8 args fell through to the
        // generic indirect call below and silently lost the receiver.
        lastValue_ = builder_.createCallWithThis(func, boxedObj, args, HIRType::makeAny());
        return;
    }

    // Generic case - callee is an expression (IIFE, function expression, etc.)
    // Lower the callee expression to get the function/closure pointer
    auto calleeVal = lowerExpression(node->callee.get());

    // Determine return type from the callee's function type if available
    std::shared_ptr<HIRType> resultType = HIRType::makeAny();
    if (calleeVal && calleeVal->type && calleeVal->type->kind == HIRTypeKind::Function && calleeVal->type->returnType) {
        resultType = calleeVal->type->returnType;
    }

    // Call the function indirectly
    lastValue_ = builder_.createCallIndirect(calleeVal, args, resultType);
}

void ASTToHIR::visitNewExpression(ast::NewExpression* node) {
    setSourceLine(node);
    // Get constructor/class name
    auto* ident = dynamic_cast<ast::Identifier*>(node->expression.get());
    std::string className = "Object";
    if (auto* classExpr = dynamic_cast<ast::ClassExpression*>(node->expression.get())) {
        // `new class { ... }( ... )` — immediately-instantiated anonymous
        // class expression. Register/emit the class (cache-hit if the
        // pre-pass saw it) so construction goes through the real class
        // machinery instead of the generic-Object fallback that silently
        // dropped every method and accessor.
        visitClassExpression(classExpr);
        className = lastGeneratedClassName_;
    } else if (ident) {
        // First check if this is a variable pointing to a class expression
        auto it = variableToClassName_.find(ident->name);
        if (it != variableToClassName_.end()) {
            className = it->second;  // Use the actual generated class name
        } else {
            className = ident->name;
        }
    } else if (auto* propAccess = dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())) {
        // Handle new ns.ClassName() where ns is a namespace import
        if (propAccess->expression->inferredType &&
            propAccess->expression->inferredType->kind == ts::TypeKind::Namespace) {
            className = propAccess->name;
            // The module property may DECLARE a differently-named extension
            // class (inspector.Session : InspectorSession). findType below
            // needs the TYPE name, not the property name — otherwise the ctor
            // lookup misses and `new inspector.Session()` degrades to a
            // dynamic new on an undefined module-map entry.
            if (auto* nsIdent = dynamic_cast<ast::Identifier*>(propAccess->expression.get())) {
                auto& extReg = ext::ExtensionRegistry::instance();
                const ext::PropertyDefinition* pd =
                    extReg.findObjectProperty(nsIdent->name, propAccess->name);
                if (pd && !pd->type.name.empty() && extReg.findType(pd->type.name)) {
                    className = pd->type.name;
                }
            }
        }
    }

    // DYNAMIC callee (`new (await X)()`, `new (f())()`, `new (c ? A : B)()`):
    // the callee is not an Identifier/PropertyAccess/ClassExpression, so no
    // class NAME can be derived (className above stays at its "Object"
    // default) — construct through the runtime from the callee VALUE.
    // Previously this fell into the generic-Object fallback, which emitted a
    // bare map and dropped the callee's construction semantics entirely
    // (module-code/top-level-await/new-await-parens: `new (await Map)()` was
    // a plain object). Callee evaluates BEFORE the arguments (ES 13.3.5.1).
    if (!dynamic_cast<ast::Identifier*>(node->expression.get()) &&
        !dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get()) &&
        !dynamic_cast<ast::ClassExpression*>(node->expression.get())) {
        auto ctorVal = boxValueIfNeeded(lowerExpression(node->expression.get()));
        auto anyArr = HIRType::makeArray(HIRType::makeAny(), false);
        auto packed = builder_.createCall("ts_array_create", {}, anyArr);
        for (auto& a : node->arguments) {
            // A SpreadElement expands into the packed args via the iterator
            // protocol — pushing the spread SOURCE as one element gave
            // `new (fnexpr)(...[])` a phantom argument.
            if (auto* spread = dynamic_cast<ast::SpreadElement*>(a.get())) {
                auto sv = lowerExpression(spread->expression.get());
                builder_.createCall("ts_array_spread_into",
                                    {packed, boxValueIfNeeded(sv)}, anyArr);
                continue;
            }
            auto v = lowerExpression(a.get());
            builder_.createCall("ts_array_push", {packed, boxValueIfNeeded(v)},
                                HIRType::makeInt64());
        }
        lastValue_ = builder_.createCall("ts_construct_apply",
            {ctorVal, packed}, HIRType::makeAny());
        return;
    }

    // Handle built-in Array class
    if (className == "Array") {
        // new Array() or new Array(length) or new Array(elem1, elem2, ...)
        // Try to infer element type from type parameter
        std::shared_ptr<HIRType> elemType = HIRType::makeAny();
        if (node->inferredType && node->inferredType->kind == ts::TypeKind::Array) {
            auto arrayType = std::static_pointer_cast<ts::ArrayType>(node->inferredType);
            if (arrayType->elementType) {
                elemType = convertType(arrayType->elementType);
            }
        }

        if (node->arguments.empty()) {
            // new Array() - create empty array
            auto zero = builder_.createConstInt(0);
            lastValue_ = builder_.createNewArrayBoxed(zero, elemType);
        } else if (node->arguments.size() == 1) {
            // Check if single argument is a number (length) or element
            auto& arg = node->arguments[0];
            bool isNumericArg = arg->inferredType &&
                (arg->inferredType->kind == ts::TypeKind::Double ||
                 arg->inferredType->kind == ts::TypeKind::Int);
            if (isNumericArg) {
                // new Array(length) - create array with capacity
                auto lenVal = lowerExpression(arg.get());
                lastValue_ = builder_.createNewArrayBoxed(lenVal, elemType);
            } else {
                // Unknown type — route through ts_array_constructor which
                // does the JS-spec runtime dispatch: numeric arg → length,
                // else → single element. This matches `new Array(x)` spec
                // semantics in untyped JS mode where `x` might be either.
                auto argVal = lowerExpression(arg.get());
                lastValue_ = builder_.createCall("ts_array_constructor",
                                                  {argVal}, HIRType::makeAny());
            }
        } else {
            // new Array(elem1, elem2, ...) - create array with elements
            auto zero = builder_.createConstInt(0);
            auto arr = builder_.createNewArrayBoxed(zero, elemType);
            for (auto& arg : node->arguments) {
                auto elemVal = lowerExpression(arg.get());
                builder_.createCall("ts_array_push", {arr, elemVal}, HIRType::makeInt64());
            }
            lastValue_ = arr;
        }
        return;
    }

    // Handle TypedArray constructors
    if (className == "Uint8Array" || className == "Int8Array" ||
        className == "Uint8ClampedArray" || className == "Int16Array" ||
        className == "Uint16Array" || className == "Int32Array" ||
        className == "Uint32Array" || className == "Float32Array" ||
        className == "Float64Array" || className == "BigInt64Array" ||
        className == "BigUint64Array") {
        std::shared_ptr<HIRValue> argVal;
        bool argIsNonInt = false;  // arg is Any/Array/Object pointer, not a known number
        if (!node->arguments.empty()) {
            argVal = lowerExpression(node->arguments[0].get());
            if (argVal && argVal->type) {
                if (argVal->type->kind == HIRTypeKind::Float64) {
                    // ECMA-262 ToIndex on the length: a raw FPToSI is UB on
                    // NaN (-> INT64_MIN -> huge length -> OOM/RangeError). Map
                    // NaN -> 0 first (d != d is true only for NaN), then FPToSI;
                    // negative / huge fall to the allocator's RangeError.
                    auto isNaN = builder_.createCmpNeF64(argVal, argVal);
                    auto zeroF = builder_.createConstFloat(0.0);
                    argVal = builder_.createSelect(isNaN, zeroF, argVal);
                    argVal = builder_.createCastF64ToI64(argVal);
                } else if (argVal->type->kind == HIRTypeKind::String ||
                           argVal->type->kind == HIRTypeKind::Any ||
                           argVal->type->kind == HIRTypeKind::Array ||
                           argVal->type->kind == HIRTypeKind::Object ||
                           argVal->type->kind == HIRTypeKind::Map ||
                           argVal->type->kind == HIRTypeKind::Set ||
                           argVal->type->kind == HIRTypeKind::Class ||
                           argVal->type->kind == HIRTypeKind::Function) {
                    // String/Any/Array/Object: route through the dispatcher,
                    // which ToNumber-coerces a string and copies array-likes.
                    // (A string-TYPED arg previously matched no branch and the
                    // raw TsString* was passed as the int64 length.)
                    argIsNonInt = true;
                }
            }
        } else {
            argVal = builder_.createConstInt(0);
        }
        // byteOffset (default 0) and byteLength (default -1 = "rest of buffer")
        // are honored only by the dispatcher when arg is an ArrayBuffer.
        std::shared_ptr<HIRValue> byteOffset = (node->arguments.size() > 1)
            ? lowerExpression(node->arguments[1].get())
            : builder_.createConstInt(0);
        std::shared_ptr<HIRValue> byteLength = (node->arguments.size() > 2)
            ? lowerExpression(node->arguments[2].get())
            : builder_.createConstInt(-1);
        // BigInt element types carry BOXED TsBigInt values through element
        // reads/writes (ts_array_get_unchecked returns a boxed BigInt; an
        // Int64 element type would make the lowering unbox it as a number →
        // INT64_MIN garbage). Any keeps the box intact end-to-end.
        bool isBigTA = (className == "BigInt64Array" ||
                        className == "BigUint64Array");
        auto arrType = HIRType::makeArray(
            isBigTA ? HIRType::makeAny() : HIRType::makeInt64(), true);
        const char* fn = nullptr;
        const char* wrapperFn = nullptr;
        if (className == "Uint8Array")             { fn = "ts_typed_array_create_u8";      wrapperFn = "ts_typed_array_new_u8"; }
        else if (className == "Uint32Array")       { fn = "ts_typed_array_create_u32";     wrapperFn = "ts_typed_array_new_u32"; }
        else if (className == "Float64Array")      { fn = "ts_typed_array_create_f64";     wrapperFn = "ts_typed_array_new_f64"; }
        else if (className == "Uint8ClampedArray") { fn = "ts_typed_array_create_clamped"; wrapperFn = "ts_typed_array_new_clamped"; }
        else if (className == "Int8Array")         { fn = "ts_typed_array_create_i8";      wrapperFn = "ts_typed_array_new_i8"; }
        else if (className == "Int16Array")        { fn = "ts_typed_array_create_i16";     wrapperFn = "ts_typed_array_new_i16"; }
        else if (className == "Uint16Array")       { fn = "ts_typed_array_create_u16";     wrapperFn = "ts_typed_array_new_u16"; }
        else if (className == "Int32Array")        { fn = "ts_typed_array_create_i32";     wrapperFn = "ts_typed_array_new_i32"; }
        else if (className == "Float32Array")      { fn = "ts_typed_array_create_f32";     wrapperFn = "ts_typed_array_new_f32"; }
        else if (className == "BigInt64Array")     { fn = "ts_typed_array_create_i64";     wrapperFn = "ts_typed_array_new_i64"; }
        else if (className == "BigUint64Array")    { fn = "ts_typed_array_create_u64";     wrapperFn = "ts_typed_array_new_u64"; }
        if (argIsNonInt && wrapperFn) {
            // Dispatcher: arg might be an ArrayBuffer (share buffer),
            // a TypedArray (copy), an Array (copy), or a number (length-only).
            lastValue_ = builder_.createCall(wrapperFn,
                {argVal, byteOffset, byteLength}, arrType);
        } else if (fn) {
            lastValue_ = builder_.createCall(fn, {argVal}, arrType);
        }
        return;
    }

    // "use fast" NativeArray<T> — unmanaged native container (docs/design/
    // use-fast.md Phase 2). `new NativeArray<T>(length[, allocator])` lowers to
    // ts_native_array_new(length, allocKind). Element type T (Int64 -> i64
    // slots, else f64) is carried on the result HIRType so get/set pick the
    // right runtime slot accessor. Gated on fastCode_ so the name is inert
    // outside fast files.
    if (fastCode_ && className == "NativeArray") {
        // Element type from the type argument string (raw source string),
        // including sized-slot widths (u8/i16/u32/f32/... -> numericBits).
        auto elemType = fastNativeElemType(
            node->typeArguments.empty() ? std::string() : node->typeArguments[0]);

        // Coerce a numeric HIR value to Int64 (length / allocator are i64 in
        // the ts_native_array_new signature).
        auto toI64 = [&](std::shared_ptr<HIRValue> v) -> std::shared_ptr<HIRValue> {
            if (!v || !v->type) return v;
            if (v->type->kind == HIRTypeKind::Float64) return builder_.createCastF64ToI64(v);
            if (v->type->kind == HIRTypeKind::Bool) return builder_.createCastBoolToI64(v);
            return v;  // Int64 (or already integral)
        };

        // Arg 0 = length. Arg 1 = allocator (Temp=0, Persistent=1); accepts
        // Allocator.Temp/.Persistent sugar or a plain numeric literal.
        std::shared_ptr<HIRValue> lengthVal =
            node->arguments.empty() ? builder_.createConstInt(0)
                                    : toI64(lowerExpression(node->arguments[0].get()));

        std::shared_ptr<HIRValue> allocVal;
        if (node->arguments.size() > 1) {
            auto* argExpr = node->arguments[1].get();
            int allocConst = -1;
            if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(argExpr)) {
                if (auto* baseId = dynamic_cast<ast::Identifier*>(pa->expression.get())) {
                    if (baseId->name == "Allocator") {
                        if (pa->name == "Temp") allocConst = 0;
                        else if (pa->name == "Persistent") allocConst = 1;
                    }
                }
            }
            allocVal = (allocConst >= 0) ? builder_.createConstInt(allocConst)
                                         : toI64(lowerExpression(argExpr));
        } else {
            allocVal = builder_.createConstInt(0);  // default Allocator.Temp
        }

        // Pack the element byte size into the allocKind word's second byte
        // (kind | bytes << 8) — the 16-byte header layout can't grow (inline
        // lowering hardcodes length@+8, slots@+16), and the runtime needs the
        // size for dispose/quarantine/arena-scrub byte math. 0 = legacy 8.
        {
            unsigned bytes = 8;
            if (elemType->numericBits) bytes = elemType->numericBits / 8;
            if (bytes != 8) {
                auto packed = builder_.createConstInt((int64_t)bytes << 8);
                allocVal = builder_.createOrI64(allocVal, packed);
            }
        }

        auto naType = HIRType::makeClass("NativeArray");
        naType->elementType = elemType;
        lastValue_ = builder_.createCall("ts_native_array_new",
                                         {lengthVal, allocVal}, naType);
        return;
    }

    // Handle built-in Map class
    if (className == "Map") {
        if (node->arguments.empty()) {
            lastValue_ = builder_.createCall("ts_map_create_explicit", {}, HIRType::makeMap());
        } else {
            // new Map(iterable) — populate from [k,v] pairs per ECMA-262 24.1.1.1
            auto iter = lowerExpression(node->arguments[0].get());
            iter = boxValueIfNeeded(iter);
            lastValue_ = builder_.createCall("ts_map_create_from_iterable", {iter}, HIRType::makeMap());
        }
        return;
    }

    // Handle built-in Set class
    if (className == "Set") {
        if (node->arguments.empty()) {
            lastValue_ = builder_.createCall("ts_set_create", {}, HIRType::makeSet());
        } else {
            // new Set(iterable) — populate from the iterable per ECMA-262 24.2.1.1
            auto iter = lowerExpression(node->arguments[0].get());
            iter = boxValueIfNeeded(iter);
            lastValue_ = builder_.createCall("ts_set_create_from_iterable", {iter}, HIRType::makeSet());
        }
        return;
    }

    // `new Function(p1, ..., pn, body)` — EVAL-001: pass ALL arguments (same
    // path as the call form). Body = LAST argument per 20.2.1.1.
    if (className == "Function") {
        auto fcArgs = builder_.createCall(
            "ts_array_create", {}, HIRType::makeArray(HIRType::makeAny(), false));
        for (auto& a : node->arguments) {
            auto boxed = boxValueIfNeeded(lowerExpression(a.get()));
            builder_.createCall("ts_array_push", {fcArgs, boxed},
                                HIRType::makeInt64());
        }
        lastValue_ = builder_.createCall("ts_function_constructor_args",
                                         {fcArgs}, HIRType::makeAny());
        return;
    }

    // Handle built-in WeakMap class (uses TsWeakMap with WMAP magic)
    if (className == "WeakMap") {
        lastValue_ = builder_.createCall("ts_weakmap_create", {}, HIRType::makeMap());
        return;
    }

    // Handle built-in WeakSet class (implemented as Set wrapper with distinct magic)
    if (className == "WeakSet") {
        lastValue_ = builder_.createCall("ts_weakset_create", {}, HIRType::makeSet());
        return;
    }

    // Handle built-in WeakRef class
    if (className == "WeakRef") {
        if (!node->arguments.empty()) {
            auto target = lowerExpression(node->arguments[0].get());
            lastValue_ = builder_.createCall("ts_weakref_create", {target}, HIRType::makeClass("WeakRef", 0));
        } else {
            lastValue_ = builder_.createCall("ts_weakref_create",
                {builder_.createConstNull()}, HIRType::makeClass("WeakRef", 0));
        }
        return;
    }

    // Handle built-in FinalizationRegistry class
    if (className == "FinalizationRegistry") {
        if (!node->arguments.empty()) {
            auto callback = lowerExpression(node->arguments[0].get());
            lastValue_ = builder_.createCall("ts_finalization_registry_create", {callback},
                HIRType::makeClass("FinalizationRegistry", 0));
        } else {
            lastValue_ = builder_.createCall("ts_finalization_registry_create",
                {builder_.createConstNull()}, HIRType::makeClass("FinalizationRegistry", 0));
        }
        return;
    }

    // Handle built-in RegExp class
    if (className == "RegExp") {
        std::shared_ptr<HIRValue> patternArg;
        std::shared_ptr<HIRValue> flagsArg;
        if (!node->arguments.empty()) {
            patternArg = lowerExpression(node->arguments[0].get());
        } else {
            patternArg = builder_.createConstString("");
        }
        if (node->arguments.size() >= 2) {
            flagsArg = lowerExpression(node->arguments[1].get());
        } else {
            flagsArg = builder_.createConstNull();
        }
        lastValue_ = builder_.createCall("ts_regexp_create", {patternArg, flagsArg}, HIRType::makeObject());
        return;
    }

    // Handle built-in Date class
    if (className == "Date") {
        if (node->arguments.empty()) {
            // new Date() - current time
            lastValue_ = builder_.createCall("ts_date_create", {}, HIRType::makeClass("Date", 0));
        } else if (node->arguments.size() == 1) {
            auto arg = lowerExpression(node->arguments[0].get());
            auto& argNode = node->arguments[0];
            bool isNumericArg = false;
            if (argNode->inferredType &&
                (argNode->inferredType->kind == ts::TypeKind::Double ||
                 argNode->inferredType->kind == ts::TypeKind::Int)) {
                isNumericArg = true;
            } else if (dynamic_cast<ast::NumericLiteral*>(argNode.get())) {
                isNumericArg = true;
            }
            if (isNumericArg) {
                // new Date(milliseconds)
                lastValue_ = builder_.createCall("ts_date_create_ms", {arg}, HIRType::makeClass("Date", 0));
            } else {
                // new Date(dateString)
                lastValue_ = builder_.createCall("ts_date_create_str", {arg}, HIRType::makeClass("Date", 0));
            }
        } else {
            // new Date(y, m [, d, h, mi, s, ms]) - ECMA-262 §21.4.2.1 step 3.
            // Missing d defaults to 1, others default to 0.
            std::vector<std::shared_ptr<HIRValue>> partsArgs;
            partsArgs.reserve(7);
            for (size_t i = 0; i < 7; ++i) {
                if (i < node->arguments.size()) {
                    partsArgs.push_back(lowerExpression(node->arguments[i].get()));
                } else {
                    partsArgs.push_back(builder_.createConstFloat(i == 2 ? 1.0 : 0.0));
                }
            }
            lastValue_ = builder_.createCall("ts_date_create_parts", partsArgs,
                                             HIRType::makeClass("Date", 0));
        }
        return;
    }

    // ArrayBuffer: `new ArrayBuffer(byteLength)` allocates a real
    // TsBuffer of the requested size. Without this dedicated path, the
    // generic ctor route allocated an empty TsMap and ignored the
    // length, leaving .byteLength undefined and downstream TypedArray
    // operations broken.
    if (className == "ArrayBuffer") {
        std::shared_ptr<HIRValue> length;
        if (!node->arguments.empty()) {
            length = lowerExpression(node->arguments[0].get());
        } else {
            length = builder_.createConstInt(0);
        }
        // ES2024 resizable buffers: `new ArrayBuffer(n, { maxByteLength })`.
        // The second argument was silently DROPPED here, so every resizable
        // buffer came out non-resizable (resizable=false, resize() a no-op).
        if (node->arguments.size() > 1) {
            auto options = boxValueIfNeeded(
                lowerExpression(node->arguments[1].get()));
            lastValue_ = builder_.createCall("ts_arraybuffer_create_with_options",
                {length, options}, HIRType::makeAny());
            return;
        }
        lastValue_ = builder_.createCall("ts_arraybuffer_create",
            {length}, HIRType::makeAny());
        return;
    }

    // DataView: `new DataView(buffer, byteOffset?, byteLength?)` wraps
    // an existing ArrayBuffer with a byte-typed view. If no buffer
    // argument is given, the runtime call throws TypeError.
    if (className == "DataView") {
        std::shared_ptr<HIRValue> buf = !node->arguments.empty()
            ? lowerExpression(node->arguments[0].get())
            : builder_.createConstNull();
        std::shared_ptr<HIRValue> byteOffset = (node->arguments.size() > 1)
            ? lowerExpression(node->arguments[1].get())
            : builder_.createConstInt(0);
        // -1 sentinel = "rest of buffer" in ts_dataview_create_full.
        std::shared_ptr<HIRValue> byteLength = (node->arguments.size() > 2)
            ? lowerExpression(node->arguments[2].get())
            : builder_.createConstInt(-1);
        lastValue_ = builder_.createCall("ts_dataview_create_full",
            {buf, byteOffset, byteLength}, HIRType::makeAny());
        return;
    }

    // AggregateError has signature (errors, message?), unlike the (message)
    // signature of all other built-in Error subclasses. Route to a dedicated
    // runtime that builds the .errors array.
    if (className == "AggregateError") {
        std::shared_ptr<HIRValue> errors;
        std::shared_ptr<HIRValue> message;
        if (!node->arguments.empty()) {
            errors = lowerExpression(node->arguments[0].get());
        } else {
            errors = builder_.createConstString("");  // will be ignored runtime-side
        }
        if (node->arguments.size() >= 2) {
            message = lowerExpression(node->arguments[1].get());
        } else {
            message = builder_.createConstString("");
        }
        lastValue_ = builder_.createCall("ts_error_create_aggregate",
            {errors, message}, HIRType::makeAny());
        return;
    }

    // Handle built-in Error classes
    if (className == "Error" || className == "TypeError" || className == "RangeError" ||
        className == "ReferenceError" || className == "SyntaxError" || className == "URIError" ||
        className == "EvalError") {
        // new Error(message) or new Error(message, { cause: ... })
        std::shared_ptr<HIRValue> message;
        if (!node->arguments.empty()) {
            message = lowerExpression(node->arguments[0].get());
        } else {
            // Create empty string
            message = builder_.createConstString("");
        }

        // Call ts_error_create or ts_error_create_typed_js (returns already-boxed TsValue*)
        if (node->arguments.size() >= 2) {
            // ES2022: Error with options { cause: ... }
            auto options = lowerExpression(node->arguments[1].get());
            lastValue_ = builder_.createCall("ts_error_create_with_options", {message, options}, HIRType::makeAny());
        } else if (className != "Error") {
            // Typed error (TypeError, RangeError, etc.) — set correct .name and .constructor
            auto nameStr = builder_.createConstString(className);
            lastValue_ = builder_.createCall("ts_error_create_typed_js", {nameStr, message}, HIRType::makeAny());
        } else {
            lastValue_ = builder_.createCall("ts_error_create", {message}, HIRType::makeAny());
        }
        return;
    }

    // TextEncoder() - no arguments
    if (className == "TextEncoder") {
        lastValue_ = builder_.createCall("ts_text_encoder_create", {}, HIRType::makeObject());
        return;
    }

    // TextDecoder(label?, options?)
    if (className == "TextDecoder") {
        std::vector<std::shared_ptr<HIRValue>> decoderArgs;
        if (!node->arguments.empty()) {
            decoderArgs.push_back(lowerExpression(node->arguments[0].get()));
        } else {
            decoderArgs.push_back(builder_.createConstNull());
        }
        // fatal and ignoreBOM default to false
        auto falseVal = builder_.createConstBool(false);
        decoderArgs.push_back(falseVal);
        decoderArgs.push_back(falseVal);
        if (node->arguments.size() >= 2) {
            // TODO: extract fatal and ignoreBOM from options object
        }
        lastValue_ = builder_.createCall("ts_text_decoder_create", decoderArgs, HIRType::makeObject());
        return;
    }

    // Spread in `new C(...args, x)`: the general path below lowers each argument
    // positionally, so a SpreadElement would pass the whole array as one arg.
    // Build the argument array via the iterator protocol (mirroring the call
    // spread path) and construct via ts_construct_apply.
    {
        bool newHasSpread = false;
        for (auto& a : node->arguments)
            if (dynamic_cast<ast::SpreadElement*>(a.get())) { newHasSpread = true; break; }
        if (newHasSpread) {
            auto anyArr = HIRType::makeArray(HIRType::makeAny(), false);
            auto packed = builder_.createCall("ts_array_create", {}, anyArr);
            for (auto& a : node->arguments) {
                auto v = lowerExpression(a.get());
                if (dynamic_cast<ast::SpreadElement*>(a.get()))
                    packed = builder_.createCall("ts_array_spread_into", {packed, boxValueIfNeeded(v)}, anyArr);
                else
                    builder_.createCall("ts_array_push", {packed, boxValueIfNeeded(v)}, HIRType::makeInt64());
            }
            auto ctorVal = lowerExpression(node->expression.get());
            lastValue_ = builder_.createCall("ts_construct_apply",
                {boxValueIfNeeded(ctorVal), packed}, HIRType::makeAny());
            return;
        }
    }

    // Lower constructor arguments
    std::vector<std::shared_ptr<HIRValue>> args;
    for (auto& arg : node->arguments) {
        args.push_back(lowerExpression(arg.get()));
    }

    // Look up the class - prefer one with constructor set (handles duplicate
    // HIRClass from spec pre-pass vs visitClassDeclaration)
    HIRClass* hirClass = nullptr;
    for (auto& cls : module_->classes) {
        if (cls->name == className) {
            hirClass = cls.get();
            if (hirClass->constructor) break;  // Found one with constructor
        }
    }
    {
        int count = 0;
        for (auto& cls : module_->classes) {
            if (cls->name == className) {
                SPDLOG_WARN("visitNewExpression: class[{}]={} ctor={} methods={} shape={}",
                    count++, className,
                    cls->constructor ? cls->constructor->name : "null",
                    cls->methods.size(), cls->shape ? "yes" : "no");
            }
        }
    }

    // Check if this is an extension type with a constructor (e.g., URL, URLSearchParams).
    // SKIP the extension lookup if the user has defined a function declaration with
    // the same name — that user function shadows the extension. Lodash's
    // `runInContext` declares `function Hash() {...}` which would otherwise resolve
    // to the crypto.Hash extension and throw "Illegal constructor".
    //
    // We deliberately do NOT shadow on a generic variable binding: patterns like
    // `var EventEmitter = events.EventEmitter; new EventEmitter()` need to resolve
    // to the extension's constructor, not to the variable's runtime value.
    bool userShadowsExtension = false;
    if (ident) {
        for (const auto& f : module_->functions) {
            if (f->name == ident->name || f->displayName == ident->name) {
                userShadowsExtension = true;
                break;
            }
        }
        if (!userShadowsExtension && specializations_) {
            for (const auto& spec : *specializations_) {
                if (spec.originalName == ident->name) {
                    userShadowsExtension = true;
                    break;
                }
            }
        }
    }
    if (!hirClass && !userShadowsExtension) {
        auto& extReg = ext::ExtensionRegistry::instance();
        const ext::TypeDefinition* extType = extReg.findType(className);
        if (extType) {
            if (extType->constructor && !extType->constructor->call.empty()) {
                // Extension type with a constructor - call the factory function directly
                std::string hirName = extType->constructor->hirName
                    ? *extType->constructor->hirName
                    : extType->constructor->call;

                // The constructor is a static factory function that returns the
                // object. Type the result as the extension CLASS (not an untyped
                // ptr) so downstream member access / indexing dispatches against
                // the right runtime shape — e.g. `new Buffer(..)[i]` must lower
                // to ts_buffer_read_uint8, not ts_array_get_unchecked (which
                // reads a TsBuffer as a TsArray and crashes). Mirrors how the
                // static factory `Buffer.from(..)` is typed (extTypeRefToHIR).
                lastValue_ = builder_.createCall(hirName, args, HIRType::makeClass(className, 0));
                return;
            }
            // Phase 9i Bug 3: extension type exists but its contract has no
            // `constructor` block. Two interpretations:
            //   - Node.js intends this class to be internal-only and its
            //     runtime constructor body throws TypeError (the majority case
            //     for crypto.Hash, http.IncomingMessage, all zlib.*, etc.)
            //   - We forgot to wire a real C runtime constructor into the
            //     schema (the minority case for legitimately new-able classes
            //     like net.Socket).
            // Both cases must throw a TypeError at the `new` site, because
            // there is no way to materialize the correct underlying C++ object
            // without a runtime constructor function. The previous behavior
            // (silent fall-through to ts_map_create) produced a TsMap with the
            // wrong shape, leading to memory corruption when the receiver was
            // later passed to a method that dereferenced it via vtable offset.
            SPDLOG_WARN("visitNewExpression: extension type '{}' has no constructor "
                        "in its contract. Emitting TypeError throw at runtime "
                        "(matches Node.js behavior for internal-only classes). "
                        "If this class SHOULD be publicly constructable, add a "
                        "`constructor` block to its .ext.json pointing at the "
                        "C runtime factory function.", className);
            auto nameStr = builder_.createConstString("TypeError");
            auto msgStr = builder_.createConstString(
                "Illegal constructor: " + className + " cannot be constructed directly");
            auto err = builder_.createCall(
                "ts_error_create_typed_js", {nameStr, msgStr}, HIRType::makeAny());
            builder_.createThrow(err);
            // Sentinel result for downstream visitor protocol (Throw is
            // unreachable but the builder still expects lastValue_ to be set).
            lastValue_ = builder_.createConstUndefined();
            return;
        }
    }

    // Create new object with the correct type
    std::shared_ptr<HIRValue> newObj;
    // `class MySet extends Set {}` (and Map/WeakSet/WeakMap/Array): the
    // instance must BE the builtin exotic object — a flat instance makes
    // every inherited method throw "called on incompatible receiver".
    // Only safe for FIELDLESS subclasses (compiled field access assumes
    // flat slot offsets); classes with fields keep the legacy flat layout.
    bool subclassBuiltinAlloc = false;
    if (hirClass && !hirClass->baseBuiltinName.empty() &&
        (!hirClass->shape || hirClass->shape->propertyOffsets.empty())) {
        const std::string& b = hirClass->baseBuiltinName;
        if (b == "Set" || b == "Map" || b == "WeakSet" || b == "WeakMap" ||
            b == "Array") {
            auto baseNameC = builder_.createConstString(b);
            auto ctorVal2 = builder_.createLoadFunction(
                hirClass->constructor ? hirClass->constructor->name
                                      : (className + "_constructor"));
            newObj = builder_.createCall("ts_subclass_builtin_alloc",
                                         {baseNameC, ctorVal2}, HIRType::makeAny());
            subclassBuiltinAlloc = true;
        }
    }
    if (subclassBuiltinAlloc) {
        // allocation done above
    } else if (hirClass && hirClass->shape && hirClass->shape->id != UINT32_MAX) {
        // Use flat object layout for class instances with registered shapes
        newObj = builder_.createNewObjectDynamic(hirClass->shape.get());
        // Set type to Class so codegen can find the vtable
        if (newObj && newObj->type) {
            newObj->type = HIRType::makeClass(className, hirClass->shape->id);
        }
    } else if (hirClass && hirClass->shape) {
        // Class with shape but no properties (no flat object)
        newObj = builder_.createNewObject(hirClass->shape.get());
    } else if (!hirClass && (ident
                              || dynamic_cast<ast::PropertyAccessExpression*>(node->expression.get())
                              || dynamic_cast<ast::ElementAccessExpression*>(node->expression.get())
                              || dynamic_cast<ast::BinaryExpression*>(node->expression.get())
                              || dynamic_cast<ast::ParenthesizedExpression*>(node->expression.get())
                              || dynamic_cast<ast::FunctionExpression*>(node->expression.get())
                              || dynamic_cast<ast::CallExpression*>(node->expression.get()))) {
        // Unknown class — treat as a constructor function call. Examples:
        //   - `new Foo()` where Foo is `function Foo() {...}` from imported JS
        //   - `new Array.prototype.concat([])` (property-access into a built-in
        //     prototype method, which is a non-constructor and must throw via
        //     the runtime `is_constructor` check).
        //   - `new holder[key]()` (computed-member receiver, e.g. lodash's
        //     `new mapCaches[kind]()` cache-interface tests) — the constructor
        //     is resolved dynamically by ElementAccess and dispatched through
        //     ts_new_from_constructor_N so `this` is bound and the prototype is
        //     linked. Without this, it fell to the plain-dynamic-object
        //     fallback: the ctor never ran and methods saw the wrong `this`.
        //   - `new (memoize.Cache || MapCache)()` (computed-receiver pattern
        //     used by lodash; receiver is a logical-or BinaryExpression wrapped
        //     in parens, not an Identifier/PropertyAccess).
        // ts_new_from_constructor_N performs the [[Construct]] dispatch and
        // throws TypeError if the target's is_constructor flag is false.
        if (ident) {
            SPDLOG_DEBUG("visitNewExpression: receiver '{}' has no registered HIRClass — "
                         "lowering to ts_new_from_constructor_N.",
                         ident->name);
        }
        auto constructorVal = lowerExpression(node->expression.get());
        if (constructorVal) {
            // Unified construct path (ts_new_from_constructor argc/argv) sets up
            // the prototype chain and calls the constructor with this = new
            // object. Replaces the by-name ts_new_from_constructor_N family,
            // which silently dropped any arguments past the 8th.
            lastValue_ = builder_.createConstruct(constructorVal, args, HIRType::makeAny());
            return;
        }
        // Expression couldn't be lowered, fall back to plain dynamic object
        newObj = builder_.createNewObjectDynamic();
    } else {
        // Fallback to dynamic object (for built-in or unknown classes)
        newObj = builder_.createNewObjectDynamic();
    }

    // Propagate escape analysis from AST
    if (!node->escapes) {
        builder_.markLastNonEscaping();
    }

    if (hirClass && hirClass->constructor) {
        // Build constructor call args: [this, ...args]. Truncate or pad to
        // match the constructor's declared arity — verifier rejects extras
        // and missing args are undefined.
        HIRFunction* ctor = hirClass->constructor;
        size_t expectedUserArgs = ctor->params.empty() ? 0 : ctor->params.size() - 1;
        std::vector<std::shared_ptr<HIRValue>> ctorArgs;
        ctorArgs.push_back(newObj);  // 'this' is the new object
        if (ctor->hasRestParam) {
            for (auto& arg : args) ctorArgs.push_back(arg);
        } else {
            for (size_t i = 0; i < expectedUserArgs; ++i) {
                if (i < args.size()) ctorArgs.push_back(args[i]);
                else ctorArgs.push_back(builder_.createConstUndefined());
            }
        }

        // ECMA-262 §10.1.1: new C() sets instance.[[Prototype]] = C.prototype.
        // For TsMap-backed instances (classes without a registered shape, or
        // shape but no fields), the prototype slot lives on the TsMap and
        // must be filled here. Flat-object instances no-op on this call (the
        // prototype is derived from ShapeDescriptor.constructorSlot).
        {
            auto ctorVal = builder_.createLoadFunction(ctor->name);
            auto protoKey = builder_.createConstString("prototype");
            auto protoVal = builder_.createCall(
                "ts_object_get_dynamic", {ctorVal, protoKey}, HIRType::makeAny());
            builder_.createCall(
                "ts_object_setPrototypeOf", {newObj, protoVal}, HIRType::makeVoid());
        }

        // Call the constructor. For a DERIVED JS class, the constructor already
        // returns its constructed value (the IR returns `ptr`), so capture it and
        // honor ECMA-262 §10.2.2: `return <object>` makes that object the result
        // of `new`. Non-derived classes keep the void call (their ctor returns
        // void; widening that convention regressed other tests).
        bool ctorIsJs = ctor->sourceFile.size() >= 3 &&
                        ctor->sourceFile.substr(ctor->sourceFile.size() - 3) == ".js";
        // ES NewTarget: the compiler-inlined construct path must also set the
        // ambient register (ts_new_from_constructor does it for the dynamic
        // path). Swap in the class constructor, restore after the ctor call.
        auto ntCtorVal = builder_.createLoadFunction(ctor->name);
        auto prevNT = builder_.createCall("ts_set_new_target",
            {ntCtorVal}, HIRType::makeAny());
        if (hirClass->baseClass && ctorIsJs) {
            auto ctorResult = builder_.createCall(ctor->name, ctorArgs, HIRType::makeAny());
            if (ctorResult)
                newObj = builder_.createCall("ts_construct_select", {ctorResult, newObj}, HIRType::makeAny());
        } else {
            builder_.createCall(ctor->name, ctorArgs, HIRType::makeVoid());
        }
        builder_.createCall("ts_set_new_target", {prevNT}, HIRType::makeAny());
    } else if (hirClass && !hirClass->constructor && specializations_) {
        // The HIRClass was created (e.g., by pre-pass for imported classes) but the
        // constructor function hasn't been generated yet. Look through specializations
        // to find the constructor and emit the call by name.
        std::string ctorName;
        for (const auto& spec : *specializations_) {
            if (spec.originalName == "constructor" && spec.classType) {
                auto ct = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                if (ct && ct->name == className) {
                    ctorName = spec.specializedName;
                    SPDLOG_WARN("visitNewExpression: found ctor spec '{}' for class '{}'",
                        ctorName, className);
                    break;
                }
            }
        }
        if (ctorName.empty()) {
            SPDLOG_WARN("visitNewExpression: NO ctor spec found for '{}' in {} specializations",
                className, specializations_->size());
            for (const auto& spec : *specializations_) {
                if (spec.classType) {
                    auto ct = std::dynamic_pointer_cast<ts::ClassType>(spec.classType);
                    if (ct && ct->name == className) {
                        SPDLOG_WARN("  spec: original='{}' specialized='{}' class='{}'",
                            spec.originalName, spec.specializedName, ct->name);
                    }
                }
            }
        }
        if (!ctorName.empty()) {
            std::vector<std::shared_ptr<HIRValue>> ctorArgs;
            ctorArgs.push_back(newObj);  // 'this' is the new object
            for (auto& arg : args) {
                ctorArgs.push_back(arg);
            }
            // Check if this is a JS slow-path constructor (from .js file).
            // Only JS constructors can return objects per [[Construct]] semantics.
            // Typed TS constructors always return void.
            bool isJsConstructor = false;
            if (node->expression) {
                // Check if the class declaration comes from a .js file
                for (const auto& spec : *specializations_) {
                    if (spec.specializedName == ctorName && spec.node) {
                        auto sf = spec.node->sourceFile;
                        if (sf.size() >= 3 && sf.substr(sf.size() - 3) == ".js") {
                            isJsConstructor = true;
                        }
                        break;
                    }
                }
            }

            if (isJsConstructor) {
                // Call with ptr return type — per JS spec, if a constructor
                // returns an object, 'new' uses that object instead of 'this'.
                auto ctorResult = builder_.createCall(ctorName, ctorArgs, HIRType::makeAny());
                if (ctorResult) {
                    auto isUndef = builder_.createCall("ts_value_is_undefined",
                        {ctorResult}, HIRType::makeBool());
                    int blockId = blockCounter_++;
                    auto* useCtor = builder_.createBlock("new_ctor_ret_" + std::to_string(blockId));
                    auto* useThis = builder_.createBlock("new_use_this_" + std::to_string(blockId));
                    auto* mergeNew = builder_.createBlock("new_merge_" + std::to_string(blockId));
                    builder_.createCondBranch(isUndef, useThis, useCtor);

                    builder_.setInsertPoint(useCtor);
                    currentBlock_ = useCtor;
                    builder_.createBranch(mergeNew);

                    builder_.setInsertPoint(useThis);
                    currentBlock_ = useThis;
                    builder_.createBranch(mergeNew);

                    builder_.setInsertPoint(mergeNew);
                    currentBlock_ = mergeNew;
                    newObj = builder_.createPhi(HIRType::makeAny(),
                        {{ctorResult, useCtor}, {newObj, useThis}});
                }
            } else {
                // Typed TS constructor — always void, always use 'this'
                builder_.createCall(ctorName, ctorArgs, HIRType::makeVoid());
            }
        }
    }

    // The result is the new object (or the constructor's return value)
    // `class Err extends TypeError {}` with a DEFAULT constructor: the
    // implicit super(...args) must run the builtin base's constructor steps
    // on the instance (Error family: own non-enumerable `message`).
    // User-defined constructors handle their own super(); builtin bases with
    // no HIR base class otherwise skip initialization entirely.
    if (hirClass && !hirClass->baseBuiltinName.empty() && newObj &&
        (!hirClass->constructor || hirClass->hasSyntheticCtor)) {
        auto baseNameC = builder_.createConstString(hirClass->baseBuiltinName);
        auto argcC = builder_.createConstInt((int64_t)args.size());
        std::shared_ptr<HIRValue> a0 = args.empty()
            ? builder_.createConstUndefined() : boxValueIfNeeded(args[0]);
        builder_.createCall("ts_super_builtin_call",
            {newObj, baseNameC, argcC, a0}, HIRType::makeVoid());
    }

    lastValue_ = newObj;
}


}  // namespace ts::hir
