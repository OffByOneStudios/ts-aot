#include <algorithm>
#include "ASTToHIR_Internal.h"

namespace ts::hir {

// Does a computed-key expression read a binding (variable/global) or build a
// value at runtime, vs. fold to a compile-time constant? A constant key
// (`[1+1]`, `["a"]`) is installable at the hoisted static-init flush; a key that
// references a binding (`[x]`, `[x &&= 1]`, `[Math.f()]`, `[()=>{}]`) must be
// evaluated at the class source position, after that binding is initialized.
// NOTE: `&&=`/`+` both parse as BinaryExpression, so recurse rather than match
// on kind — a binary of literals is constant; a binary touching an identifier is not.
// Conservative "might this derived-constructor body call super()?" scanner.
// Polarity matters: we emit the must-call-super ReferenceError only when we
// are CERTAIN there is no super() reachable — any node kind we don't model
// counts as "might" (returns true) so no valid program ever gets the throw.
// Nested non-arrow functions get their own [[HomeObject]]/this and cannot
// satisfy the requirement, but scanning them as "might" is safely lenient.
bool stmtMightCallSuper(ast::Statement* s);
static bool exprMightCallSuper(ast::Expression* e) {
    if (!e) return false;
    std::string k = e->getKind();
    if (k == "SuperExpression") return true;   // super(...) / super.x
    if (k == "Identifier" || k == "NumericLiteral" || k == "StringLiteral" ||
        k == "BooleanLiteral" || k == "NullLiteral" || k == "BigIntLiteral" ||
        k == "ThisExpression" || k == "RegularExpressionLiteral")
        return false;
    if (auto* c = dynamic_cast<ast::CallExpression*>(e)) {
        if (exprMightCallSuper(c->callee.get())) return true;
        for (auto& a : c->arguments)
            if (exprMightCallSuper(a.get())) return true;
        return false;
    }
    if (auto* b = dynamic_cast<ast::BinaryExpression*>(e))
        return exprMightCallSuper(b->left.get()) || exprMightCallSuper(b->right.get());
    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(e))
        return exprMightCallSuper(p->expression.get());
    return true;   // anything else: assume it might
}
bool stmtMightCallSuper(ast::Statement* s) {
    if (!s) return false;
    if (auto* es = dynamic_cast<ast::ExpressionStatement*>(s))
        return exprMightCallSuper(es->expression.get());
    if (auto* bs = dynamic_cast<ast::BlockStatement*>(s)) {
        for (auto& st : bs->statements)
            if (stmtMightCallSuper(st.get())) return true;
        return false;
    }
    if (auto* is = dynamic_cast<ast::IfStatement*>(s))
        return exprMightCallSuper(is->condition.get()) ||
               stmtMightCallSuper(is->thenStatement.get()) ||
               stmtMightCallSuper(is->elseStatement.get());
    if (auto* rs = dynamic_cast<ast::ReturnStatement*>(s))
        return exprMightCallSuper(rs->expression.get());
    return true;   // loops/try/decls/etc.: assume they might
}

static bool computedKeyReferencesBinding(ast::Expression* e) {
    if (!e) return false;
    std::string k = e->getKind();
    if (k == "Identifier") return true;
    if (k == "NumericLiteral" || k == "StringLiteral" || k == "BooleanLiteral" ||
        k == "BigIntLiteral" || k == "NullLiteral" || k == "RegularExpressionLiteral")
        return false;
    // yield/await are only valid in the enclosing generator/async context; they
    // can't be hoisted to a module-init install trigger, so leave them on the
    // existing path (don't route).
    if (k == "YieldExpression" || k == "AwaitExpression") return false;
    if (auto* b = dynamic_cast<ast::BinaryExpression*>(e))
        return computedKeyReferencesBinding(b->left.get()) ||
               computedKeyReferencesBinding(b->right.get());
    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(e))
        return computedKeyReferencesBinding(p->expression.get());
    return true;  // arrow/function/call/assignment/member/etc. — evaluate at source position
}


void ASTToHIR::installClassMember(std::shared_ptr<HIRValue> recv,
                                  const std::string& key,
                                  std::shared_ptr<HIRValue> closure) {
    // Private methods ("#m") must never be own property keys (ECMA-262: private
    // names are not property keys). Store under the hidden "\x01#m" key; the
    // runtime get paths consult it for '#'-literal lookups and enumeration skips
    // '\x01'-prefixed keys.
    std::string storageKey = key;
    if (!key.empty() && key[0] == '#') {
        storageKey = std::string("\x01") + key;
    }
    auto keyStr = builder_.createConstString(storageKey);
    builder_.createCall("ts_object_set_method",
        {recv, keyStr, closure}, HIRType::makeVoid());
}

std::string ASTToHIR::completeMethodSymbol(HIRClass* hirClass, const std::string& methodKey,
                                           HIRFunction* fallback, bool isStatic) {
    // The Monomorphizer lowers a get/set accessor body into "<Class>_set_<name>" /
    // "<Class>_get_<name>" (the symbol the instance vtable uses), while
    // hirClass->methods often holds a separate EMPTY module-level placeholder
    // "<Class>___setter_<name>" (body == `ret void`). The deferred install runs
    // before the vtable is re-registered with the complete function, so resolve the
    // monomorphized symbol by name directly and install that — otherwise a
    // setter/getter invoked via `C.prototype[key]` runs the no-op placeholder.
    if (hirClass) {
        std::string smark = isStatic ? "static_" : "";
        std::string cand;
        if (methodKey.rfind("__setter_", 0) == 0)
            cand = hirClass->name + "_" + smark + "set_" + methodKey.substr(9);
        else if (methodKey.rfind("__getter_", 0) == 0)
            cand = hirClass->name + "_" + smark + "get_" + methodKey.substr(9);
        // The monomorphized C_set_/C_get_ body is emitted AFTER this deferred
        // install runs, so it isn't in module_ yet — reference it by name and let
        // the linker resolve it (it is present in the final module, used by the
        // instance vtable). Redirect ONLY when the methods-map entry is an empty
        // module-level STUB (body is just `ret`, the real body was monomorphized
        // under cand). A literal-named accessor (`get 0x10`) keeps its complete
        // body right here and must NOT be redirected.
        if (!cand.empty() && fallback && fallback->name != cand) {
            size_t instrCount = 0;
            for (auto& b : fallback->blocks) instrCount += b->instructions.size();
            if (instrCount <= 1) return cand;  // stub placeholder → use the real body
        }
    }
    return fallback ? fallback->mangledName : std::string();
}

std::string ASTToHIR::computeClassMethodFuncName(const std::string& className,
                                                 ast::MethodDefinition* methodDef,
                                                 bool isComputedAccessor,
                                                 int& computedAccessorSeq,
                                                 std::string& outMethodKey) {
    // Static `constructor` is a static method, not the instance ctor — it must
    // NOT share the canonical "<Class>_constructor" symbol (would collide with
    // the real instance constructor and crash codegen on duplicate symbols).
    outMethodKey = methodDef->name;  // Key used for registration in class
    if (isComputedAccessor) {
        return className + "___computed_acc_" + std::to_string(computedAccessorSeq++);
    } else if (methodDef->name == "constructor" && !methodDef->isStatic) {
        return className + "_constructor";
    } else if (methodDef->isGetter) {
        // The SYMBOL must match the Monomorphizer's accessor spec name
        // (`<Class>_get_<name>` / `<Class>_static_get_<name>`, Monomorphizer.cpp
        // ~2474/2492). Methods already converge on one symbol, so the spec pass
        // REPLACES the first-pass HIRFunction and its real body wins. Accessors
        // used to diverge (`<Class>___getter_<name>`), leaving TWO functions: the
        // vtable got the good one, but the __getter_ closure slot — which the
        // runtime property-get walk consults first — wrapped the first-pass body,
        // where module-level identifiers fold to a constant undefined. Net effect:
        // every class accessor read module scope as undefined.
        // outMethodKey (the storage key) stays __getter_/__setter_ — only the
        // symbol changes.
        // STATIC accessors keep the legacy symbol: converging them on the
        // Monomorphizer's `_static_get_` name regressed 494 class tests
        // (statics take no `this` param, so the spec function that then
        // replaces this one has a different signature and static accessor
        // dispatch breaks — e.g. `static get method(){ return this.#method; }`
        // stopped invoking, leaving callCount 0). Static accessors don't
        // exhibit the module-scope bug in practice, so leave them alone.
        outMethodKey = "__getter_" + methodDef->name;
        return className + (methodDef->isStatic ? "___getter_" : "_get_") + methodDef->name;
    } else if (methodDef->isSetter) {
        outMethodKey = "__setter_" + methodDef->name;
        return className + (methodDef->isStatic ? "___setter_" : "_set_") + methodDef->name;
    } else if (methodDef->isStatic) {
        return className + "_static_" + methodDef->name;
    } else {
        return className + "_" + methodDef->name;
    }
}

void ASTToHIR::visitClassDeclaration(ast::ClassDeclaration* node) {
    setSourceLine(node);
    SPDLOG_WARN("visitClassDeclaration: name={} currentFunc={}",
        node->name, currentFunction_ ? currentFunction_->name : "null");

    // Create HIR class
    auto* hirClass = builder_.createClass(node->name);
    if (!hirClass) return;
    hirClass->isStruct = node->isStruct;  // "use fast" value type

    // Track the current class for 'this' handling
    HIRClass* savedClass = currentClass_;
    currentClass_ = hirClass;

    // Lexical private-name scope (ES PrivateEnvironment): collect this
    // class's declared #names so member bodies (lowered inline below)
    // resolve them to per-class storage keys — nested classes' same-named
    // privates get distinct brands (shadowed-by-nested-class family).
    {
        PrivateClassCtx pctx;
        pctx.id = hirClass->name;
        for (auto& m : node->members) {
            if (auto* pd = dynamic_cast<ast::PropertyDefinition*>(m.get())) {
                if (!pd->name.empty() && pd->name[0] == '#') pctx.fields.insert(pd->name);
            } else if (auto* md = dynamic_cast<ast::MethodDefinition*>(m.get())) {
                if (!md->name.empty() && md->name[0] == '#') {
                    pctx.others.insert(md->name);
                    if (!md->isGetter && !md->isSetter) pctx.methods.insert(md->name);
                    if (md->isGetter) pctx.getters.insert(md->name);
                    if (md->isGetter || md->isSetter) pctx.accessors.insert(md->name);
                }
            }
        }
        privateClassStack_.push_back(std::move(pctx));
        classPrivSnapshots_[hirClass->name] = privateClassStack_;
    }
    // ES 10.2.1: ClassBody is ALWAYS strict code — member bodies lowered
    // inline below must emit strict write semantics (PutValue throw=true).
    bool savedStrictCode = strictCode_;
    strictCode_ = true;
    {
    }

    // Handle inheritance - look up base class
    if (!node->baseClass.empty()) {
        for (auto& cls : module_->classes) {
            if (cls->name == node->baseClass) {
                hirClass->baseClass = cls.get();
                break;
            }
        }
        // Heritage that isn't a user class (extends Set / Error / ...):
        // remember the NAME so the class flush can link the prototype
        // chain to the builtin at runtime.
        if (!hirClass->baseClass) hirClass->baseBuiltinName = node->baseClass;
    }

    // Create class shape (layout of instance properties)
    auto shape = std::make_shared<HIRShape>();
    shape->className = node->name;

    // First pass: collect properties for the shape
    uint32_t propertyOffset = 0;

    // If we have a base class, copy its properties first
    if (hirClass->baseClass && hirClass->baseClass->shape) {
        auto baseShape = hirClass->baseClass->shape;
        shape->parent = baseShape.get();
        // Copy base class properties
        for (const auto& [name, offset] : baseShape->propertyOffsets) {
            shape->propertyOffsets[name] = offset;
        }
        for (const auto& [name, type] : baseShape->propertyTypes) {
            shape->propertyTypes[name] = type;
        }
        propertyOffset = baseShape->size;  // Start our properties after base class properties
    }

    // Add this class's own properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (!propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                // Computed-name fields (`[expr] = v`) are dynamic properties, not
                // fixed shape slots — their key is only known at runtime.
                if (propDef->name != "[computed]") {
                    // Private fields shape-key under the class-qualified name
                    // so the flat slot ("" + key at HIRToLLVM) matches
                    // the class-qualified writes/reads.
                    std::string shapeKey = resolvePrivateName(propDef->name);
                    shape->propertyOffsets[shapeKey] = propertyOffset;
                    shape->propertyTypes[shapeKey] = propType;
                    propertyOffset++;
                }
            }
        }
    }

    // Scan instance constructor body for this.x = expr assignments
    // (static-method "constructor" is unrelated).
    for (auto& memberPtr : node->members) {
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (method->name == "constructor" && method->hasBody && !method->isStatic) {
                scanConstructorBodyForProperties(method->body, shape, propertyOffset);
                break;
            }
        }
    }

    shape->size = propertyOffset;
    hirClass->shape = shape;

    // Register class shape for flat object codegen if it has properties or instance methods.
    // Classes with methods but no PropertyDefinition fields (e.g., JS classes where properties
    // are assigned in the constructor body) still need flat objects for vtable method dispatch.
    {
        bool hasInstanceMethods = false;
        for (auto& memberPtr : node->members) {
            if (auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (md->name != "constructor" && !md->isStatic && !md->isAbstract && md->hasBody) {
                    hasInstanceMethods = true;
                    break;
                }
            }
        }
        if (!shape->propertyOffsets.empty() || hasInstanceMethods) {
            shape->id = nextShapeId_++;
            module_->shapes.push_back(shape);
        }
    }

    // Static property pass: create globals for static properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                std::string globalName = node->name + "_static_" + propDef->name;

                // Create global variable for the static property
                auto globalPtr = builder_.createGlobal(globalName, propType);
                staticPropertyGlobals_[globalName] = {globalPtr, propType};

                // Defer initialization to user_main
                if (propDef->initializer) {
                    // Mirror onto the constructor closure (own property) so the
                    // static field is reachable through an alias / dynamic key /
                    // passed reference. ctorName is always "<Class>_constructor".
                    deferredStaticInits_.push_back({globalPtr, propType, propDef->initializer.get(),
                                                    node->name + "_constructor", propDef->name,
                                                    propDef->name == "[computed]" ? propDef->nameNode.get() : nullptr,
                                                    privateClassStack_});
                } else if (!propDef->name.empty() && propDef->name[0] == '#') {
                    // ES 15.7: a static private field establishes the class's
                    // static PrivateBrand at class evaluation even with NO
                    // initializer (`static #x;`). Install `\x01#x@Class` = undefined
                    // on the constructor now so the static brand check
                    // (ts_object_{get,set}_private on a constructor receiver)
                    // distinguishes the declaring class from a subclass / foreign
                    // receiver. A null initExpr means "store undefined" in the drain.
                    deferredStaticInits_.push_back({globalPtr, propType, nullptr,
                                                    node->name + "_constructor", propDef->name,
                                                    nullptr, privateClassStack_});
                }
            }
        }
        // Collect static blocks for deferred execution
        if (auto* staticBlock = dynamic_cast<ast::StaticBlock*>(memberPtr.get())) {
            deferredStaticBlocks_.push_back({staticBlock, privateClassStack_});
        }
    }

    // Inherit abstract methods from base class
    if (hirClass->baseClass) {
        hirClass->abstractMethods = hirClass->baseClass->abstractMethods;
    }

    // Track abstract methods declared in this class and pre-register concrete methods.
    // Pre-registration ensures that when lowering method bodies, calls to other methods
    // in the same class (defined later) can be found in the methods map.
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (methodDef->isAbstract) {
                hirClass->abstractMethods.insert(methodDef->name);
            } else if (methodDef->hasBody && methodDef->name != "constructor") {
                // Concrete method overrides abstract - remove from set
                hirClass->abstractMethods.erase(methodDef->name);
                // Pre-register with nullptr so forward references resolve
                std::string methodKey = methodDef->name;
                if (methodDef->isGetter) methodKey = "__getter_" + methodDef->name;
                else if (methodDef->isSetter) methodKey = "__setter_" + methodDef->name;
                if (!methodDef->isStatic && hirClass->methods.find(methodKey) == hirClass->methods.end()) {
                    hirClass->methods[methodKey] = nullptr;
                }
            }
        }
    }

    // Second pass: create methods
    int computedAccessorSeq = 0;
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            // Skip abstract methods - they have no body
            if (methodDef->isAbstract || !methodDef->hasBody) {
                continue;
            }

            // Computed-name accessor (`get [expr]()` / `set [expr]()`): the
            // storage key is only known once the key expression is evaluated at
            // class-definition time, so it can't go in the string-keyed methods
            // map. Give the function a collision-free symbol and route it to
            // hirClass->computedAccessors for runtime install in the deferred
            // prototype-build pass.
            // Any computed-name member (accessor OR regular method) can't use a
            // static string key; route all of them through the runtime-install
            // path. Regular methods install without the __getter_/__setter_
            // prefix (isMethod below). Previously only accessors were handled,
            // so `class C { [1](){} }` was stored under the literal key
            // "[computed]" and `new C()[1]()` read undefined.
            bool isComputedName =
                dynamic_cast<ast::ComputedPropertyName*>(methodDef->nameNode.get());

            // Generate a unique function name for the method (shared with the
            // class-expression path via computeClassMethodFuncName).
            std::string methodKey;
            std::string methodFuncName = computeClassMethodFuncName(
                node->name, methodDef, isComputedName, computedAccessorSeq, methodKey);

            // Create HIR function for this method
            auto func = std::make_unique<HIRFunction>(methodFuncName);
            func->isAsync = methodDef->isAsync;
            func->isGenerator = methodDef->isGenerator;
            func->sourceLine = methodDef->line;
            func->sourceFile = methodDef->sourceFile;
            // SetFunctionName: a class method's .name is its key (accessors are
            // prefixed "get "/"set "); the instance constructor's .name is the
            // class name (inferred binding name for an anonymous class expr).
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                std::string cn = node->name.empty() ? pendingClosureDisplayName_ : node->name;
                if (!cn.empty()) func->displayName = cn;
            } else if (!methodDef->name.empty()) {
                func->displayName = methodDef->isGetter ? ("get " + methodDef->name)
                                  : methodDef->isSetter ? ("set " + methodDef->name)
                                  : methodDef->name;
            }

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Collect destructured parameter patterns so we can emit
            // extraction at method entry — without this, `class C {
            // method([x, y, z]) {} }` produces a method with a single
            // `paramN` and no destructuring, leaving x/y/z unbound and
            // crashing on use. Mirrors the FunctionDeclaration handling.
            struct CClsDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<CClsDestructuredParam> ccDestructuredParams;

            // Add explicit parameters
            size_t mdUserIdx = 0;
            for (auto& param : methodDef->parameters) {
                // TypeScript `this` parameter is type-only; the implicit
                // `this` formal is already pushed above. Keeping it would
                // shift every real parameter by one slot at runtime.
                if (param->isThisParameter) continue;
                // ECMA-262 10.2.5 fn.length: index of the first non-simple
                // (default/rest/destructured) user param. Class-EXPRESSION
                // methods use THIS inline HIRFunction (the Monomorphizer does
                // not re-specialize anonymous class members), so without this
                // firstNonSimpleParamIndex stays SIZE_MAX and .length counts
                // default params (`method(a,b=1)` -> 2 instead of 1). Index is
                // in user params (this/__closure__/__arg excluded), matching
                // the arity loop in HIRToLLVM_Closures.cpp.
                if (func->firstNonSimpleParamIndex == SIZE_MAX) {
                    bool mdIsDestr =
                        dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                        dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
                    if (param->initializer || param->isRest || mdIsDestr)
                        func->firstNonSimpleParamIndex = mdUserIdx;
                }
                mdUserIdx++;
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }
                func->params.push_back({paramName, paramType});
            }

            // If the method body uses `arguments`, pad with hidden __argN__
            // params so call args beyond the declared count physically reach
            // ts_create_arguments_from_params (mirrors the
            // FunctionDeclaration path; without this arguments[N] beyond the
            // declared params read as undefined).
            {
                bool mBodyUsesArgs = false;
                for (auto& stmt : methodDef->body) {
                    if (containsArgumentsIdentifier(stmt.get())) { mBodyUsesArgs = true; break; }
                }
                if (!mBodyUsesArgs) mBodyUsesArgs = paramsReferenceArguments(methodDef->parameters);
                if (mBodyUsesArgs) {
                    while (func->params.size() < 10) {
                        std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                        func->params.push_back({argName, HIRType::makeAny()});
                    }
                }
            }

            // Set return type
            // Setters always return void, regardless of explicit type annotation
            if (methodDef->isSetter) {
                func->returnType = HIRType::makeVoid();
            } else {
                func->returnType = methodDef->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(methodDef->returnType);
            }

            // Save current function and create entry block
            HIRFunction* savedFunc = currentFunction_;
            bool savedMethodStatic = currentMethodIsStatic_;
            currentMethodIsStatic_ = methodDef->isStatic;
    // A nested function body starts OUTSIDE any enclosing try/with:
    // tryDepth_/withDepth_ are lowering-state of the OUTER function. A
    // `return` in a closure defined inside a try{} otherwise emits a
    // PopHandler for a handler the closure never pushed — at runtime it
    // popped the CALLER's exception handler (found via the Promise
    // combinator spec-path whose reject-handler vanished).
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering
    // state. This site previously saved only try/with fields — nested
    // bodies saw the parent loop/label/break stacks and pending
    // captures (latent leak from the audit).
    std::optional<FunctionLoweringScope> flsScope{std::in_place, *this};
            currentFunction_ = func.get();

            // Create entry block
            auto entryBlock = func->createBlock("entry");
            builder_.setInsertPoint(entryBlock);
            currentBlock_ = entryBlock;

            // Enter function scope
            pushFunctionScope(func.get());

            // Register parameters in scope.
            // ccArgTypeOffset is 1 for instance methods (slot 0 = synthetic
            // 'this', user params start at HIR index 1) and 0 for static
            // methods. Map the HIR param index back to the AST parameter so
            // we honor default-value initializers (e.g. `method(a = 99)`).
            // The InliningPass searches module_->functions by name and picks
            // the first match — visitClassDeclaration emits this body BEFORE
            // the spec path, so this body must include the default-handling
            // branch or the inliner will fold the call site to raw `undefined`.
            //
            // CRITICAL: set nextValueId BEFORE the loop so allocas created
            // for default-handling don't collide with param HIRValue ids
            // (params already occupy ids [0..N-1]).
            func->nextValueId = static_cast<uint32_t>(func->params.size());
            // ECMA-262 parameter TDZ (preseedParamTDZ): later/self param
            // reads inside a default throw ReferenceError.
            preseedParamTDZ(func.get(), methodDef->parameters);
            size_t ccArgTypeOffset = methodDef->isStatic ? 0 : 1;
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                size_t astParamIdx = (i >= ccArgTypeOffset) ? (i - ccArgTypeOffset) : SIZE_MAX;
                ast::Parameter* astParam = (astParamIdx < methodDef->parameters.size())
                    ? methodDef->parameters[astParamIdx].get() : nullptr;
                bool isDestructured = astParam && (
                    dynamic_cast<ast::ObjectBindingPattern*>(astParam->name.get()) ||
                    dynamic_cast<ast::ArrayBindingPattern*>(astParam->name.get()));

                if (astParam && astParam->initializer && !isDestructured) {
                    // Scalar default — alloca + branch on isUndefined, assign
                    // default expression value when missing.
                    auto allocaVal = builder_.createAlloca(paramType);
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    auto defaultBB = func->createBlock("default_param");
                    auto usedBB = func->createBlock("use_param");
                    auto mergeBB = func->createBlock("param_merge");

                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr)
                                               : builder_.createConstUndefined();
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    defineVariable(paramName, paramValue);
                }
            }
            // NOTE: Do NOT reset nextValueId here. The default-handling logic
            // above creates allocas and intermediate values that bumped
            // nextValueId past params.size(); resetting it here would cause
            // the destructure loop below to re-use ids and collide.

            // Emit destructuring extraction for parameters with binding
            // patterns (mirrors the FunctionDeclaration path).
            for (auto& dp : ccDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    func->params[dp.paramIndex].first);
                if (auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer)) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto defaultVal = lowerExpression(defaultExpr);
                    defaultVal = boxValueIfNeeded(defaultVal);
                    paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // Async generators: end of PARAMETER prologue — body throws after
            // this reject the first next() promise (ts_agen_should_reject).
            // Mirrors the FunctionDeclaration/arrow/funcExpr/method sites.
            if (func->isAsync && func->isGenerator) {
                builder_.createCall("ts_async_generator_body_started", {},
                                    HIRType::makeVoid());
            } else if (func->isGenerator) {
                // Sync generator: eager-parameter model (marker = suspension).
                builder_.createCall("ts_generator_body_started", {},
                                    HIRType::makeVoid());
            }

            // For instance constructors, initialize instance property defaults before user code.
            // Static `constructor` is just a static method — never an instance ctor.
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic) {
                                // ECMA-262 15.7: every declared field
                                // is installed on the instance, even
                                // when no initializer is given — value
                                // defaults to undefined. Without this,
                                // tests like `class { 'a'; 'b' = 1; }`
                                // see only 'b' as an own property.
                                std::shared_ptr<HIRValue> initVal;
                                if (propDef->initializer) {
                                    {
                                        // Field-initializer eval context
                                        // (ES ClassFieldDefinition: eval code
                                        // containing 'arguments' -> Syntax-
                                        // Error). flags bit3; owner check
                                        // keeps nested fn bodies plain.
                                        int savedAEF = activeEvalFlags_;
                                        HIRFunction* savedAEO = evalFlagsOwner_;
                                        activeEvalFlags_ |= 12;  // bit2 strict + bit3 field-init
                                        evalFlagsOwner_ = currentFunction_;
                                        initVal = lowerExpression(propDef->initializer.get());
                                        activeEvalFlags_ = savedAEF;
                                        evalFlagsOwner_ = savedAEO;
                                    }
                                } else {
                                    initVal = builder_.createConstUndefined();
                                }
                                emitInstanceFieldSet(thisValue, propDef, initVal);
                            }
                        }
                    }
                }
            }

            // 'arguments' object for METHOD bodies (mirrors the
            // FunctionDeclaration prologue): class-expression methods lower
            // here without the Monomorphizer spec path, so `arguments` was
            // never synthesized — every trailing-comma/args test over
            // class-expr (async-)generator methods threw ReferenceError.
            {
                bool mUsesArguments = false;
                for (auto& stmt : methodDef->body) {
                    if (containsArgumentsIdentifier(stmt.get())) { mUsesArguments = true; break; }
                }
                if (!mUsesArguments) mUsesArguments = paramsReferenceArguments(methodDef->parameters);
                if (mUsesArguments && !lookupVariableInfoInCurrentFunction("arguments")) {
                    std::vector<std::shared_ptr<HIRValue>> callArgs;
                    size_t userIdx = 0;
                    for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
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
                    auto argsArray = builder_.createCall("ts_create_arguments_from_params",
                        callArgs, HIRType::makeAny());
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
                    builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
                    defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
                }
            }

            // Lower method body with JavaScript hoisting: var pre-declaration,
            // let/const TDZ pre-declaration, nested function-name hoist, and a
            // two-pass walk (function declarations first) — mirrors the
            // spec-function body lowering in ASTToHIR.cpp. Without this a
            // nested function declaration captured nothing (`let self = this;
            // function inner(){ self.#m = v; }` read `self` as undefined).
            {
                std::vector<std::string> mHoisted;
                std::vector<std::string> mHoistedFns;
                for (auto& stmt : methodDef->body)
                    collectHoistedVarNames(stmt.get(), mHoisted, &mHoistedFns);
                {
                    // Annex B B.3.3: suppress the var-copy for fn names that clash
                    // with a top-level lexical declaration.
                    std::set<std::string> lexNames_;
                    collectTopLevelLexicalNames(methodDef->body, lexNames_);
                    for (auto& fn_ : mHoistedFns)
                        if (lexNames_.count(fn_))
                            mHoisted.erase(std::remove(mHoisted.begin(), mHoisted.end(), fn_), mHoisted.end());
                    mHoistedFns.erase(std::remove_if(mHoistedFns.begin(), mHoistedFns.end(),
                        [&](const std::string& x){ return lexNames_.count(x) != 0; }), mHoistedFns.end());
                }
                for (auto& nm : mHoisted) {
                    if (lookupVariableInfoInCurrentFunction(nm)) continue;
                    auto a = builder_.createAlloca(HIRType::makeAny(), nm);
                    builder_.createStore(builder_.createConstUndefined(), a, HIRType::makeAny());
                    defineVariableAlloca(nm, a, HIRType::makeAny());
                    if (std::find(mHoistedFns.begin(), mHoistedFns.end(), nm) != mHoistedFns.end())
                        if (auto* vi = lookupVariableInfoInCurrentFunction(nm)) vi->isFnHoist = true;
                }
                for (auto& stmt : methodDef->body) {
                    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                        if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
                        auto* idn = dynamic_cast<ast::Identifier*>(vd->name.get());
                        if (!idn) continue;
                        if (lookupVariableInfoInCurrentFunction(idn->name)) continue;
                        auto a = builder_.createAlloca(HIRType::makeAny(), idn->name);
                        auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
                        builder_.createStore(tdz, a, HIRType::makeAny());
                        defineVariableAlloca(idn->name, a, HIRType::makeAny());
                        if (auto* vi = lookupVariableInfoInCurrentFunction(idn->name)) vi->isTDZ = true;
                    }
                }
                for (auto& stmt : methodDef->body)
                    if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get()))
                        lowerStatement(stmt.get());
                emitMutualRecursionFixup();
                for (auto& stmt : methodDef->body) {
                    if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) continue;
                    lowerStatement(stmt.get());
                    if (builder_.isBlockTerminated()) break;
                }
            }

            // ES 9.2.2 / 10.2.2: a DERIVED-class constructor that completes
            // normally without having called super() throws ReferenceError
            // (`this` never initialized). Emitted only when the body PROVABLY
            // contains no super() (conservative scanner) so conditional or
            // exotic bodies never get a false throw. An explicit
            // `return <object>` terminates the block first and skips this.
            if (methodDef->name == "constructor" && !methodDef->isStatic &&
                !node->baseClass.empty() && !hasTerminator()) {
                bool might = false;
                for (auto& stmt : methodDef->body)
                    if (stmtMightCallSuper(stmt.get())) { might = true; break; }
                if (!might)
                    builder_.createCall("ts_throw_super_not_called", {},
                                        HIRType::makeAny());
            }

            // Add implicit return if no terminator
            if (!hasTerminator()) {
                builder_.createReturnVoid();
            }

            popScope();

            // Restore saved function
            currentFunction_ = savedFunc;
            currentMethodIsStatic_ = savedMethodStatic;
    flsScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register method in the class. Static `constructor` is a
            // static method that happens to be named "constructor" — NOT
            // the class's instance constructor.
            HIRFunction* funcPtr = func.get();
            if (isComputedName) {
                auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(methodDef->nameNode.get());
                bool isMethod = !methodDef->isGetter && !methodDef->isSetter;
                hirClass->computedAccessors.push_back(
                    {cpn ? cpn->expression.get() : nullptr, funcPtr,
                     methodDef->isSetter, methodDef->isStatic, isMethod,
                     /*moduleLevelBody=*/currentFunction_ == nullptr});
            } else if (methodDef->name == "constructor" && !methodDef->isStatic) {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                // Use methodKey so static accessors get the
                // __getter_<name> / __setter_<name> prefix needed for
                // runtime accessor dispatch on the constructor.
                hirClass->staticMethods[methodKey] = funcPtr;
            } else {
                // Use methodKey for registration (includes __getter_/__setter_ prefix for accessors)
                hirClass->methods[methodKey] = funcPtr;
                // Add to vtable for virtual dispatch
                hirClass->vtable.push_back({methodKey, funcPtr});
            }

            // Add function to module
            module_->functions.push_back(std::move(func));
        }
    }

    // If no explicit constructor was defined, but we have property initializers,
    // generate a default constructor to initialize them
    if (!hirClass->constructor) {
        // Check if there are any property initializers
        bool hasPropertyInitializers = false;
        for (auto& memberPtr : node->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Always emit a default constructor so the class identifier
        // resolves to a real function value in untyped JS mode. Without
        // a constructor function, `typeof E` and `E.prototype` collapse
        // to undefined because visitIdentifier has nothing to load.
        // The body still calls super() when there is a base class and
        // initializes property defaults when present.
        bool needsDefaultConstructor = true;
        (void)hasPropertyInitializers;

        if (needsDefaultConstructor) {
            std::string ctorName = node->name + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);

            // 'this' is the first parameter
            defaultCtor->params.push_back({"this", HIRType::makeObject()});
            // ECMA-262 15.7.14: the implicit constructor of a DERIVED class is
            // `constructor(...args){ super(...args); }` — it forwards its
            // arguments to the parent. Mirror the base constructor's parameter
            // list (after 'this') so `class C extends A {}; new C(7,8)` reaches
            // A's constructor with 7,8. Without this the default ctor took only
            // 'this' and called super() with no args.
            HIRFunction* baseCtorFwd = (hirClass->baseClass && hirClass->baseClass->constructor)
                ? hirClass->baseClass->constructor : nullptr;
            if (baseCtorFwd) {
                for (size_t pi = 1; pi < baseCtorFwd->params.size(); ++pi) {
                    defaultCtor->params.push_back(baseCtorFwd->params[pi]);
                }
            }
            defaultCtor->nextValueId = static_cast<uint32_t>(defaultCtor->params.size());

            // Create entry block
            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            bool savedMethodStatic = currentMethodIsStatic_;
            currentMethodIsStatic_ = false;  // synthesized default ctor
    // A nested function body starts OUTSIDE any enclosing try/with:
    // tryDepth_/withDepth_ are lowering-state of the OUTER function. A
    // `return` in a closure defined inside a try{} otherwise emits a
    // PopHandler for a handler the closure never pushed — at runtime it
    // popped the CALLER's exception handler (found via the Promise
    // combinator spec-path whose reject-handler vanished).
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering
    // state. This site previously saved only try/with fields — nested
    // bodies saw the parent loop/label/break stacks and pending
    // captures (latent leak from the audit).
    std::optional<FunctionLoweringScope> flsScope{std::in_place, *this};
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            // Define 'this' in scope
            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeObject(), "this");
            defineVariable("this", thisValue);

            // Call super(...args) if we have a base class, forwarding the
            // mirrored parameters (HIR ids 1..N) to the parent constructor.
            if (hirClass->baseClass && hirClass->baseClass->constructor) {
                std::vector<std::shared_ptr<HIRValue>> superArgs;
                superArgs.push_back(thisValue);
                for (size_t pi = 1; pi < defaultCtor->params.size(); ++pi) {
                    superArgs.push_back(std::make_shared<HIRValue>(
                        static_cast<uint32_t>(pi), defaultCtor->params[pi].second,
                        defaultCtor->params[pi].first));
                }
                builder_.createCall(hirClass->baseClass->constructor->name, superArgs, HIRType::makeVoid());
            }

            // Initialize property defaults. Every declared instance
            // field is installed on `this`, with `undefined` for
            // fields without initializers — matches ECMA-262 15.7.
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic) {
                        std::shared_ptr<HIRValue> initVal;
                        if (propDef->initializer) {
                            {
                                        // Field-initializer eval context
                                        // (ES ClassFieldDefinition: eval code
                                        // containing 'arguments' -> Syntax-
                                        // Error). flags bit3; owner check
                                        // keeps nested fn bodies plain.
                                        int savedAEF = activeEvalFlags_;
                                        HIRFunction* savedAEO = evalFlagsOwner_;
                                        activeEvalFlags_ |= 12;  // bit2 strict + bit3 field-init
                                        evalFlagsOwner_ = currentFunction_;
                                        initVal = lowerExpression(propDef->initializer.get());
                                        activeEvalFlags_ = savedAEF;
                                        evalFlagsOwner_ = savedAEO;
                                    }
                        } else {
                            initVal = builder_.createConstUndefined();
                        }
                        emitInstanceFieldSet(thisValue, propDef, initVal);
                    }
                }
            }

            // Return void
            builder_.createReturnVoid();

            popScope();
            currentFunction_ = savedFunc;
            currentMethodIsStatic_ = savedMethodStatic;
    flsScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register the default constructor
            hirClass->constructor = defaultCtor.get();
            hirClass->hasSyntheticCtor = true;
            module_->functions.push_back(std::move(defaultCtor));
        }
    }

    // Generate decorator static init function if class has any decorators
    // (class decorators, method decorators, property decorators, or parameter decorators)
    generateClassDecoratorStaticInit(node->name, node->decorators, node->members);

    // Defer class-prototype install: emitDeferredStaticInits at user_main
    // entry will create a real prototype object holding all instance
    // methods (and `__getter_<key>` / `__setter_<key>` for accessors)
    // and assign it to `E.prototype`. Without this, `E.prototype` reads
    // the function's default `prototype` slot (an empty object) and
    // direct accessor probes like `E.prototype['<key>']` return
    // undefined.
    // Every class needs a prototype init (not just classes with user-defined
    // methods) so that `c.constructor === C`, `Object.getPrototypeOf(c) ===
    // C.prototype`, and `extends` linkage all hold.
    // Classes in NESTED function bodies emit their full setup INLINE at the
    // class statement's source position: heritage evaluation and computed-key
    // evaluation are ClassDefinitionEvaluation side effects that must run
    // inside the enclosing function (an enclosing try/catch must see their
    // throws; the deferred flush would run them at user_main entry instead —
    // computed-name-referenceerror / extends-TypeError families). Module-level
    // classes keep the deferred flush + Monomorphizer trigger machinery.
    {
        bool topLevelCtx = !currentFunction_ ||
            currentFunction_->name.rfind("__module_init_", 0) == 0 ||
            currentFunction_->name == "user_main" ||
            currentFunction_->name == "__synthetic_user_main";
        if (topLevelCtx) {
            deferredClassPrototypes_.push_back(hirClass);
        } else {
            emitSingleClassSetup(hirClass, /*valueResolveHeritage=*/true);
        }
    }

    // Restore class context
    if (!privateClassStack_.empty()) privateClassStack_.pop_back();
    strictCode_ = savedStrictCode;
    currentClass_ = savedClass;
}

void ASTToHIR::visitClassExpression(ast::ClassExpression* node) {
    setSourceLine(node);

    // ECMA-262 ClassDefinitionEvaluation: the inferred/binding name
    // (SetFunctionName) is applied ONLY when the class has no own `name`. A
    // static `name` element — `static name(){}`, `static name = v`,
    // `static get name(){}` — provides the class's .name, so suppress the
    // pending inferred name when one is present (`xCls = class { static name(){}
    // }` keeps the static member, not "xCls"). A non-static `name` member lives
    // on the prototype and does NOT count.
    if (!pendingClosureDisplayName_.empty()) {
        for (auto& m : node->members) {
            bool staticName = false;
            if (auto* pd = dynamic_cast<ast::PropertyDefinition*>(m.get()))
                staticName = pd->isStatic && pd->name == "name";
            else if (auto* md = dynamic_cast<ast::MethodDefinition*>(m.get()))
                staticName = md->isStatic && md->name == "name";
            if (staticName) { pendingClosureDisplayName_.clear(); break; }
        }
    }

    // Phase 9c-i: if this AST node was already pre-registered in pass 1, skip
    // straight to the trailer (loadFunction + prototype setup) and don't
    // re-create the class. The pre-pass call had no current function so the
    // trailer was skipped; this second call (from visitVariableDeclaration in
    // a function body context) is where we emit the value-producing code.
    auto cacheIt = astClassExprToHIRClass_.find(node);
    if (cacheIt != astClassExprToHIRClass_.end()) {
        HIRClass* hirClass = cacheIt->second;
        lastGeneratedClassName_ = hirClass->name;
        if (!currentFunction_) {
            // Pre-pass: also queue the prototype install so that
            // top-level `let B = class { foo(){} }` gets `B.prototype.foo`
            // populated at user_main entry (the inline trailer below
            // never runs for top-level class expressions because the
            // node is not re-visited from a function context — the
            // let-decl statement lives in `module->ast->body` only).
            // Same widening as the declaration path — every class needs init.
            {
                bool already = false;
                for (auto* c : deferredClassPrototypes_) if (c == hirClass) { already = true; break; }
                if (!already) deferredClassPrototypes_.push_back(hirClass);
            }
            return;
        }
        // Emit the value: a reference to the constructor function.
        if (hirClass->constructor) {
            lastValue_ = builder_.createLoadFunction(hirClass->constructor->name);
        } else {
            lastValue_ = builder_.createLoadFunction(hirClass->name + "_constructor");
        }
        // ECMA-262 14.6.13 ClassDefinitionEvaluation: the class expression's
        // VALUE is the constructor F. Capture it now — emitComputedAccessorInstalls
        // below lowers each computed key via lowerExpression, which overwrites
        // lastValue_ with the key string. Without restoring it, an inline
        // `(class { [k](){} }).prototype` read `.prototype` off that string
        // (-> undefined).
        auto ctorResult = lastValue_;
        // Set up prototype object with instance methods for dynamic dispatch.
        // Always build (even for an empty class body) so `A.prototype`, the
        // constructor backref, and instanceof via the prototype walk hold.
        {
            auto ctorVal = ctorResult;
            auto proto = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());
            for (auto& [methodKey, methodFunc] : hirClass->methods) {
                if (!methodFunc) continue;
                auto methodClosure = builder_.createLoadFunction(completeMethodSymbol(hirClass, methodKey, methodFunc));
                installClassMember(proto, qualifyPrivateMemberKey(methodKey, hirClass->name), methodClosure);  // non-enumerable
            }
            builder_.createSetPropStatic(ctorVal, "prototype", proto);
            installClassMember(proto, "constructor", ctorVal);
            emitComputedAccessorInstalls(hirClass, proto, ctorVal);
            // `extends` linkage (ES 15.7.14): C.prototype.[[Proto]] =
            // Base.prototype (user class or builtin). Mirrors
            // emitDeferredStaticInits — the trailer rebuild would otherwise
            // drop the chain for in-function class expressions.
            if (hirClass->baseClass) {
                std::string baseCtorName = hirClass->baseClass->constructor
                    ? hirClass->baseClass->constructor->name
                    : hirClass->baseClass->name + "_constructor";
                auto baseCtorVal = builder_.createLoadFunction(baseCtorName);
                auto basePropName = builder_.createConstString("prototype");
                auto baseProtoVal = builder_.createCall("ts_object_get_dynamic",
                    {baseCtorVal, basePropName}, HIRType::makeAny());
                builder_.createCall("ts_object_setPrototypeOf",
                    {proto, baseProtoVal}, HIRType::makeVoid());
                builder_.createCall("ts_object_setPrototypeOf",
                    {ctorVal, baseCtorVal}, HIRType::makeVoid());
            } else if (!hirClass->baseBuiltinName.empty()) {
                auto baseNameC = builder_.createConstString(hirClass->baseBuiltinName);
                builder_.createCall("ts_class_link_builtin_base",
                    {ctorVal, proto, baseNameC}, HIRType::makeVoid());
            }
        }
        // Install static methods on the constructor for dynamic access.
        for (auto& [methodName, methodFunc] : hirClass->staticMethods) {
            if (!methodFunc) continue;
            auto methodClosure = builder_.createLoadFunction(completeMethodSymbol(hirClass, methodName, methodFunc, /*isStatic=*/true));
            installClassMember(ctorResult, qualifyPrivateMemberKey(methodName, hirClass->name), methodClosure);  // non-enumerable
        }
        // Restore the class-expression value (a computed-key install above may
        // have overwritten lastValue_ with the key string).
        lastValue_ = ctorResult;
        return;
    }

    // Generate a unique class name for anonymous class expressions
    // Use the same naming convention as the analyzer (__anon_class_X)
    std::string className = node->name.empty()
        ? "__anon_class_" + std::to_string(classExprCounter_++)
        : node->name;

    // Pre-register the binding->class mapping BEFORE method bodies lower:
    // `var C = class { static m() { return C.#x; } }` — the method references
    // the enclosing binding by name, and identifier resolution serves it via
    // variableToClassName_ -> resolveClassByName (the same path a class
    // DECLARATION's name uses). visitVariableDeclaration only records the
    // mapping after the initializer returns — too late for the method bodies,
    // which read `C` as undefined (the rs-static-privatename by-classname
    // test262 family). pendingClosureDisplayName_ carries the binding name.
    if (!pendingClosureDisplayName_.empty()) {
        variableToClassName_[pendingClosureDisplayName_] = className;
    }

    // Create HIR class
    auto* hirClass = builder_.createClass(className);
    if (!hirClass) {
        lastValue_ = builder_.createConstNull();
        return;
    }
    // Phase 9c-i: cache so a re-visit (from the var-decl lowering after the
    // pre-pass) reuses this class instead of creating a duplicate.
    astClassExprToHIRClass_[node] = hirClass;

    // Track the current class for 'this' handling
    HIRClass* savedClass = currentClass_;
    currentClass_ = hirClass;

    // Lexical private-name scope (ES PrivateEnvironment): collect this
    // class's declared #names so member bodies (lowered inline below)
    // resolve them to per-class storage keys — nested classes' same-named
    // privates get distinct brands (shadowed-by-nested-class family).
    {
        PrivateClassCtx pctx;
        pctx.id = hirClass->name;
        for (auto& m : node->members) {
            if (auto* pd = dynamic_cast<ast::PropertyDefinition*>(m.get())) {
                if (!pd->name.empty() && pd->name[0] == '#') pctx.fields.insert(pd->name);
            } else if (auto* md = dynamic_cast<ast::MethodDefinition*>(m.get())) {
                if (!md->name.empty() && md->name[0] == '#') {
                    pctx.others.insert(md->name);
                    if (!md->isGetter && !md->isSetter) pctx.methods.insert(md->name);
                    if (md->isGetter) pctx.getters.insert(md->name);
                    if (md->isGetter || md->isSetter) pctx.accessors.insert(md->name);
                }
            }
        }
        privateClassStack_.push_back(std::move(pctx));
        classPrivSnapshots_[hirClass->name] = privateClassStack_;
    }
    // ES 10.2.1: ClassBody is ALWAYS strict code — member bodies lowered
    // inline below must emit strict write semantics (PutValue throw=true).
    bool savedStrictCode = strictCode_;
    strictCode_ = true;
    {
    }

    // Handle inheritance - look up base class
    if (!node->baseClass.empty()) {
        for (auto& cls : module_->classes) {
            if (cls->name == node->baseClass) {
                hirClass->baseClass = cls.get();
                break;
            }
        }
        // Heritage that isn't a user class (extends Set / Error / ...):
        // remember the NAME so the class flush can link the prototype
        // chain to the builtin at runtime.
        if (!hirClass->baseClass) hirClass->baseBuiltinName = node->baseClass;
    }

    // Create class shape (layout of instance properties)
    auto shape = std::make_shared<HIRShape>();
    shape->className = className;

    // First pass: collect properties for the shape
    uint32_t propertyOffset = 0;

    // If we have a base class, copy its properties first
    if (hirClass->baseClass && hirClass->baseClass->shape) {
        auto baseShape = hirClass->baseClass->shape;
        shape->parent = baseShape.get();
        // Copy base class properties
        for (const auto& [name, offset] : baseShape->propertyOffsets) {
            shape->propertyOffsets[name] = offset;
        }
        for (const auto& [name, type] : baseShape->propertyTypes) {
            shape->propertyTypes[name] = type;
        }
        propertyOffset = baseShape->size;  // Start our properties after base class properties
    }

    // Add this class's own properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (!propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                // Computed-name fields (`[expr] = v`) are dynamic properties, not
                // fixed shape slots — their key is only known at runtime.
                if (propDef->name != "[computed]") {
                    // Private fields shape-key under the class-qualified name
                    // so the flat slot ("" + key at HIRToLLVM) matches
                    // the class-qualified writes/reads.
                    std::string shapeKey = resolvePrivateName(propDef->name);
                    shape->propertyOffsets[shapeKey] = propertyOffset;
                    shape->propertyTypes[shapeKey] = propType;
                    propertyOffset++;
                }
            }
        }
    }

    // Scan instance constructor body for this.x = expr assignments
    // (static-method "constructor" is unrelated).
    for (auto& memberPtr : node->members) {
        if (auto* method = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            if (method->name == "constructor" && method->hasBody && !method->isStatic) {
                scanConstructorBodyForProperties(method->body, shape, propertyOffset);
                break;
            }
        }
    }

    shape->size = propertyOffset;
    hirClass->shape = shape;

    // Register class shape for flat object codegen if it has properties or instance methods
    {
        bool hasInstanceMethods = false;
        for (auto& memberPtr : node->members) {
            if (auto* md = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
                if (md->name != "constructor" && !md->isStatic && !md->isAbstract && md->hasBody) {
                    hasInstanceMethods = true;
                    break;
                }
            }
        }
        if (!shape->propertyOffsets.empty() || hasInstanceMethods) {
            shape->id = nextShapeId_++;
            module_->shapes.push_back(shape);
        }
    }

    // Static property pass: create globals for static properties
    for (auto& memberPtr : node->members) {
        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
            if (propDef->isStatic) {
                auto propType = propDef->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(propDef->type);
                std::string globalName = className + "_static_" + propDef->name;

                // Create global variable for the static property
                auto globalPtr = builder_.createGlobal(globalName, propType);
                staticPropertyGlobals_[globalName] = {globalPtr, propType};

                // A computed field name that reads a variable (`static [x] = v`)
                // can't be evaluated at the hoisted deferred flush (the variable
                // isn't bound yet). Route it through computedAccessors so it
                // installs at the source position via the install trigger, like a
                // computed accessor. Don't ALSO defer it (would double-eval init).
                ast::ComputedPropertyName* fieldCpn = nullptr;
                if (propDef->name == "[computed]")
                    fieldCpn = dynamic_cast<ast::ComputedPropertyName*>(propDef->nameNode.get());
                // A compile-time-constant key (`[1+1]`, `["a"]`) is evaluable at the
                // hoisted flush, so keep it on the deferred path (proven). A key that
                // reads a variable or builds a value (`[x]`, `[x &&= 1]`, `[() => {}]`,
                // `[f()]`) must run at the source position — route it through the
                // install trigger like a computed accessor (single eval, no deferral).
                bool runtimeKey = fieldCpn && fieldCpn->expression &&
                                  computedKeyReferencesBinding(fieldCpn->expression.get());
                if (runtimeKey && propDef->initializer) {
                    // {keyExpr, func, isSetter, isStatic, isMethod, moduleLevelBody, isField, initExpr}
                    hirClass->computedAccessors.push_back(
                        {fieldCpn->expression.get(), nullptr, false, /*isStatic=*/true,
                         false, false, /*isField=*/true, propDef->initializer.get()});
                }
                // Defer initialization to user_main
                else if (propDef->initializer) {
                    // Mirror onto the constructor closure (own property) so the
                    // static field is reachable through an alias / dynamic key /
                    // passed reference. ctorName is always "<Class>_constructor".
                    deferredStaticInits_.push_back({globalPtr, propType, propDef->initializer.get(),
                                                    className + "_constructor", propDef->name,
                                                    propDef->name == "[computed]" ? propDef->nameNode.get() : nullptr,
                                                    privateClassStack_});
                }
                else if (!propDef->name.empty() && propDef->name[0] == '#') {
                    // ES 15.7: a static private field (`static #x;`) establishes
                    // the class's static PrivateBrand at class evaluation even
                    // without an initializer. Install `\x01#x@Class` = undefined so
                    // the static brand check distinguishes the declaring class from
                    // a subclass / foreign receiver (null initExpr = store undefined).
                    deferredStaticInits_.push_back({globalPtr, propType, nullptr,
                                                    className + "_constructor", propDef->name,
                                                    nullptr, privateClassStack_});
                }
            }
        }
        // Collect static blocks for deferred execution
        if (auto* staticBlock = dynamic_cast<ast::StaticBlock*>(memberPtr.get())) {
            deferredStaticBlocks_.push_back({staticBlock, privateClassStack_});
        }
    }

    // Save the current insert point before processing methods
    // (so we can restore it after and not pollute method bodies with later instructions)
    HIRBlock* savedBlockBeforeMethods = currentBlock_;
    HIRFunction* savedFuncBeforeMethods = currentFunction_;

    // Second pass: create methods
    int computedAccessorSeq = 0;
    for (auto& memberPtr : node->members) {
        if (auto* methodDef = dynamic_cast<ast::MethodDefinition*>(memberPtr.get())) {
            // Skip abstract methods - they have no body
            if (methodDef->isAbstract || !methodDef->hasBody) {
                continue;
            }

            // Any computed-name member (accessor OR regular method) — see the
            // statements/class path: route to hirClass->computedAccessors for
            // runtime install since the storage key isn't statically known.
            bool isComputedName =
                dynamic_cast<ast::ComputedPropertyName*>(methodDef->nameNode.get());

            // Generate a unique function name for the method (shared with the
            // class-declaration path via computeClassMethodFuncName).
            std::string methodKey;
            std::string methodFuncName = computeClassMethodFuncName(
                className, methodDef, isComputedName, computedAccessorSeq, methodKey);

            // Create HIR function for this method
            auto func = std::make_unique<HIRFunction>(methodFuncName);
            func->isAsync = methodDef->isAsync;
            func->isGenerator = methodDef->isGenerator;
            func->sourceLine = methodDef->line;
            func->sourceFile = methodDef->sourceFile;
            // SetFunctionName: a class method's .name is its key (accessors are
            // prefixed "get "/"set "); the instance constructor's .name is the
            // class name (inferred binding name for an anonymous class expr).
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                std::string cn = node->name.empty() ? pendingClosureDisplayName_ : node->name;
                if (!cn.empty()) func->displayName = cn;
            } else if (!methodDef->name.empty()) {
                func->displayName = methodDef->isGetter ? ("get " + methodDef->name)
                                  : methodDef->isSetter ? ("set " + methodDef->name)
                                  : methodDef->name;
            }

            // For instance methods (and constructor), 'this' is the first parameter
            if (!methodDef->isStatic) {
                func->params.push_back({"this", HIRType::makeObject()});
            }

            // Collect destructured parameter patterns so we can emit
            // extraction at method entry — without this, `class C {
            // method([x, y, z]) {} }` produces a method with a single
            // `paramN` and no destructuring, leaving x/y/z unbound and
            // crashing on use. Mirrors the FunctionDeclaration handling.
            struct CClsDestructuredParam {
                size_t paramIndex;
                ast::ObjectBindingPattern* objPattern = nullptr;
                ast::ArrayBindingPattern* arrPattern = nullptr;
                ast::Node* defaultInitializer = nullptr;
            };
            std::vector<CClsDestructuredParam> ccDestructuredParams;

            // Add explicit parameters
            size_t mdUserIdx = 0;
            for (auto& param : methodDef->parameters) {
                // TypeScript `this` parameter is type-only; the implicit
                // `this` formal is already pushed above. Keeping it would
                // shift every real parameter by one slot at runtime.
                if (param->isThisParameter) continue;
                // ECMA-262 10.2.5 fn.length: index of the first non-simple
                // (default/rest/destructured) user param. Class-EXPRESSION
                // methods use THIS inline HIRFunction (the Monomorphizer does
                // not re-specialize anonymous class members), so without this
                // firstNonSimpleParamIndex stays SIZE_MAX and .length counts
                // default params (`method(a,b=1)` -> 2 instead of 1). Index is
                // in user params (this/__closure__/__arg excluded), matching
                // the arity loop in HIRToLLVM_Closures.cpp.
                if (func->firstNonSimpleParamIndex == SIZE_MAX) {
                    bool mdIsDestr =
                        dynamic_cast<ast::ObjectBindingPattern*>(param->name.get()) ||
                        dynamic_cast<ast::ArrayBindingPattern*>(param->name.get());
                    if (param->initializer || param->isRest || mdIsDestr)
                        func->firstNonSimpleParamIndex = mdUserIdx;
                }
                mdUserIdx++;
                auto paramType = param->type.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(param->type);

                std::string paramName;
                if (auto* ident = dynamic_cast<ast::Identifier*>(param->name.get())) {
                    paramName = ident->name;
                } else if (auto* objPat = dynamic_cast<ast::ObjectBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), objPat, nullptr,
                        param->initializer.get()});
                } else if (auto* arrPat = dynamic_cast<ast::ArrayBindingPattern*>(param->name.get())) {
                    paramName = "param" + std::to_string(func->params.size());
                    paramType = HIRType::makeAny();
                    ccDestructuredParams.push_back({func->params.size(), nullptr, arrPat,
                        param->initializer.get()});
                } else {
                    paramName = "param" + std::to_string(func->params.size());
                }
                func->params.push_back({paramName, paramType});
            }

            // If the method body uses `arguments`, pad with hidden __argN__
            // params so call args beyond the declared count physically reach
            // ts_create_arguments_from_params (mirrors the
            // FunctionDeclaration path; without this arguments[N] beyond the
            // declared params read as undefined).
            {
                bool mBodyUsesArgs = false;
                for (auto& stmt : methodDef->body) {
                    if (containsArgumentsIdentifier(stmt.get())) { mBodyUsesArgs = true; break; }
                }
                if (!mBodyUsesArgs) mBodyUsesArgs = paramsReferenceArguments(methodDef->parameters);
                if (mBodyUsesArgs) {
                    while (func->params.size() < 10) {
                        std::string argName = "__arg" + std::to_string(func->params.size()) + "__";
                        func->params.push_back({argName, HIRType::makeAny()});
                    }
                }
            }

            // Set return type
            // Setters always return void, regardless of explicit type annotation
            if (methodDef->isSetter) {
                func->returnType = HIRType::makeVoid();
            } else {
                func->returnType = methodDef->returnType.empty()
                    ? HIRType::makeAny()
                    : convertTypeFromString(methodDef->returnType);
            }

            // Save current function and create entry block
            HIRFunction* savedFunc = currentFunction_;
            bool savedMethodStatic = currentMethodIsStatic_;
            currentMethodIsStatic_ = methodDef->isStatic;
    // A nested function body starts OUTSIDE any enclosing try/with:
    // tryDepth_/withDepth_ are lowering-state of the OUTER function. A
    // `return` in a closure defined inside a try{} otherwise emits a
    // PopHandler for a handler the closure never pushed — at runtime it
    // popped the CALLER's exception handler (found via the Promise
    // combinator spec-path whose reject-handler vanished).
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering
    // state. This site previously saved only try/with fields — nested
    // bodies saw the parent loop/label/break stacks and pending
    // captures (latent leak from the audit).
    std::optional<FunctionLoweringScope> flsScope{std::in_place, *this};
            currentFunction_ = func.get();

            // Create entry block
            auto entryBlock = func->createBlock("entry");
            builder_.setInsertPoint(entryBlock);
            currentBlock_ = entryBlock;

            // Enter function scope
            pushFunctionScope(func.get());

            // Register parameters in scope.
            // ccArgTypeOffset is 1 for instance methods (slot 0 = synthetic
            // 'this', user params start at HIR index 1) and 0 for static
            // methods. Map the HIR param index back to the AST parameter so
            // we honor default-value initializers (e.g. `method(a = 99)`).
            // The InliningPass searches module_->functions by name and picks
            // the first match — visitClassDeclaration emits this body BEFORE
            // the spec path, so this body must include the default-handling
            // branch or the inliner will fold the call site to raw `undefined`.
            //
            // CRITICAL: set nextValueId BEFORE the loop so allocas created
            // for default-handling don't collide with param HIRValue ids
            // (params already occupy ids [0..N-1]).
            func->nextValueId = static_cast<uint32_t>(func->params.size());
            // ECMA-262 parameter TDZ (preseedParamTDZ): later/self param
            // reads inside a default throw ReferenceError.
            preseedParamTDZ(func.get(), methodDef->parameters);
            size_t ccArgTypeOffset = methodDef->isStatic ? 0 : 1;
            for (size_t i = 0; i < func->params.size(); ++i) {
                const auto& [paramName, paramType] = func->params[i];
                auto paramValue = std::make_shared<HIRValue>(static_cast<uint32_t>(i), paramType, paramName);

                size_t astParamIdx = (i >= ccArgTypeOffset) ? (i - ccArgTypeOffset) : SIZE_MAX;
                ast::Parameter* astParam = (astParamIdx < methodDef->parameters.size())
                    ? methodDef->parameters[astParamIdx].get() : nullptr;
                bool isDestructured = astParam && (
                    dynamic_cast<ast::ObjectBindingPattern*>(astParam->name.get()) ||
                    dynamic_cast<ast::ArrayBindingPattern*>(astParam->name.get()));

                if (astParam && astParam->initializer && !isDestructured) {
                    // Scalar default — alloca + branch on isUndefined, assign
                    // default expression value when missing.
                    auto allocaVal = builder_.createAlloca(paramType);
                    auto isUndefined = builder_.createCall("ts_value_is_undefined",
                        {paramValue}, HIRType::makeBool());

                    auto defaultBB = func->createBlock("default_param");
                    auto usedBB = func->createBlock("use_param");
                    auto mergeBB = func->createBlock("param_merge");

                    builder_.createCondBranch(isUndefined, defaultBB, usedBB);

                    builder_.setInsertPoint(defaultBB);
                    currentBlock_ = defaultBB;
                    auto* initExpr = dynamic_cast<ast::Expression*>(astParam->initializer.get());
                    auto defaultVal = initExpr ? lowerExpression(initExpr)
                                               : builder_.createConstUndefined();
                    if (paramType->kind == HIRTypeKind::Any) {
                        defaultVal = forceBoxValue(defaultVal);
                    }
                    builder_.createStore(defaultVal, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(usedBB);
                    currentBlock_ = usedBB;
                    builder_.createStore(paramValue, allocaVal);
                    builder_.createBranch(mergeBB);

                    builder_.setInsertPoint(mergeBB);
                    currentBlock_ = mergeBB;

                    defineVariableAlloca(paramName, allocaVal, paramType);
                } else {
                    defineVariable(paramName, paramValue);
                }
            }
            // NOTE: Do NOT reset nextValueId here. The default-handling logic
            // above creates allocas and intermediate values that bumped
            // nextValueId past params.size(); resetting it here would cause
            // the destructure loop below to re-use ids and collide.

            // Emit destructuring extraction for parameters with binding
            // patterns (mirrors the FunctionDeclaration path).
            for (auto& dp : ccDestructuredParams) {
                auto paramValue = std::make_shared<HIRValue>(
                    static_cast<uint32_t>(dp.paramIndex),
                    HIRType::makeAny(),
                    func->params[dp.paramIndex].first);
                if (auto* defaultExpr = dynamic_cast<ast::Expression*>(dp.defaultInitializer)) {
                    auto isUndef = builder_.createIsUndefined(paramValue);
                    auto defaultVal = lowerExpression(defaultExpr);
                    defaultVal = boxValueIfNeeded(defaultVal);
                    paramValue = builder_.createSelect(isUndef, defaultVal, paramValue);
                }
                if (dp.objPattern) {
                    lowerObjectBindingPattern(dp.objPattern, paramValue);
                } else if (dp.arrPattern) {
                    lowerArrayBindingPattern(dp.arrPattern, paramValue);
                }
            }

            // Async generators: end of PARAMETER prologue — body throws after
            // this reject the first next() promise (ts_agen_should_reject).
            // Mirrors the FunctionDeclaration/arrow/funcExpr/method sites.
            if (func->isAsync && func->isGenerator) {
                builder_.createCall("ts_async_generator_body_started", {},
                                    HIRType::makeVoid());
            } else if (func->isGenerator) {
                // Sync generator: eager-parameter model (marker = suspension).
                builder_.createCall("ts_generator_body_started", {},
                                    HIRType::makeVoid());
            }

            // For instance constructors, initialize instance property defaults before user code.
            // Static `constructor` is just a static method — never an instance ctor.
            if (methodDef->name == "constructor" && !methodDef->isStatic) {
                // Get 'this' pointer (first parameter)
                auto thisValue = lookupVariable("this");
                if (thisValue) {
                    // Iterate over all property definitions and emit initializers
                    for (auto& member : node->members) {
                        if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(member.get())) {
                            if (!propDef->isStatic) {
                                // ECMA-262 15.7: every declared field
                                // is installed on the instance, even
                                // when no initializer is given — value
                                // defaults to undefined. Without this,
                                // tests like `class { 'a'; 'b' = 1; }`
                                // see only 'b' as an own property.
                                std::shared_ptr<HIRValue> initVal;
                                if (propDef->initializer) {
                                    {
                                        // Field-initializer eval context
                                        // (ES ClassFieldDefinition: eval code
                                        // containing 'arguments' -> Syntax-
                                        // Error). flags bit3; owner check
                                        // keeps nested fn bodies plain.
                                        int savedAEF = activeEvalFlags_;
                                        HIRFunction* savedAEO = evalFlagsOwner_;
                                        activeEvalFlags_ |= 12;  // bit2 strict + bit3 field-init
                                        evalFlagsOwner_ = currentFunction_;
                                        initVal = lowerExpression(propDef->initializer.get());
                                        activeEvalFlags_ = savedAEF;
                                        evalFlagsOwner_ = savedAEO;
                                    }
                                } else {
                                    initVal = builder_.createConstUndefined();
                                }
                                emitInstanceFieldSet(thisValue, propDef, initVal);
                            }
                        }
                    }
                }
            }

            // 'arguments' object for METHOD bodies (mirrors the
            // FunctionDeclaration prologue): class-expression methods lower
            // here without the Monomorphizer spec path, so `arguments` was
            // never synthesized — every trailing-comma/args test over
            // class-expr (async-)generator methods threw ReferenceError.
            {
                bool mUsesArguments = false;
                for (auto& stmt : methodDef->body) {
                    if (containsArgumentsIdentifier(stmt.get())) { mUsesArguments = true; break; }
                }
                if (!mUsesArguments) mUsesArguments = paramsReferenceArguments(methodDef->parameters);
                if (mUsesArguments && !lookupVariableInfoInCurrentFunction("arguments")) {
                    std::vector<std::shared_ptr<HIRValue>> callArgs;
                    size_t userIdx = 0;
                    for (size_t i = 0; i < func->params.size() && userIdx < 10; ++i) {
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
                    auto argsArray = builder_.createCall("ts_create_arguments_from_params",
                        callArgs, HIRType::makeAny());
                    auto allocaVal = builder_.createAlloca(HIRType::makeAny(), "arguments");
                    builder_.createStore(argsArray, allocaVal, HIRType::makeAny());
                    defineVariableAlloca("arguments", allocaVal, HIRType::makeAny());
                }
            }

            // Lower method body with JavaScript hoisting: var pre-declaration,
            // let/const TDZ pre-declaration, nested function-name hoist, and a
            // two-pass walk (function declarations first) — mirrors the
            // spec-function body lowering in ASTToHIR.cpp. Without this a
            // nested function declaration captured nothing (`let self = this;
            // function inner(){ self.#m = v; }` read `self` as undefined).
            {
                std::vector<std::string> mHoisted;
                std::vector<std::string> mHoistedFns;
                for (auto& stmt : methodDef->body)
                    collectHoistedVarNames(stmt.get(), mHoisted, &mHoistedFns);
                for (auto& nm : mHoisted) {
                    if (lookupVariableInfoInCurrentFunction(nm)) continue;
                    auto a = builder_.createAlloca(HIRType::makeAny(), nm);
                    builder_.createStore(builder_.createConstUndefined(), a, HIRType::makeAny());
                    defineVariableAlloca(nm, a, HIRType::makeAny());
                    if (std::find(mHoistedFns.begin(), mHoistedFns.end(), nm) != mHoistedFns.end())
                        if (auto* vi = lookupVariableInfoInCurrentFunction(nm)) vi->isFnHoist = true;
                }
                for (auto& stmt : methodDef->body) {
                    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
                        if (vd->varKind != ast::VarKind::Let && vd->varKind != ast::VarKind::Const) continue;
                        auto* idn = dynamic_cast<ast::Identifier*>(vd->name.get());
                        if (!idn) continue;
                        if (lookupVariableInfoInCurrentFunction(idn->name)) continue;
                        auto a = builder_.createAlloca(HIRType::makeAny(), idn->name);
                        auto tdz = builder_.createCall("ts_tdz_sentinel", {}, HIRType::makeAny());
                        builder_.createStore(tdz, a, HIRType::makeAny());
                        defineVariableAlloca(idn->name, a, HIRType::makeAny());
                        if (auto* vi = lookupVariableInfoInCurrentFunction(idn->name)) vi->isTDZ = true;
                    }
                }
                for (auto& stmt : methodDef->body)
                    if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get()))
                        lowerStatement(stmt.get());
                emitMutualRecursionFixup();
                for (auto& stmt : methodDef->body) {
                    if (dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) continue;
                    lowerStatement(stmt.get());
                    if (builder_.isBlockTerminated()) break;
                }
            }

            // ES 9.2.2 / 10.2.2: a DERIVED-class constructor that completes
            // normally without having called super() throws ReferenceError
            // (`this` never initialized). Emitted only when the body PROVABLY
            // contains no super() (conservative scanner) so conditional or
            // exotic bodies never get a false throw. An explicit
            // `return <object>` terminates the block first and skips this.
            if (methodDef->name == "constructor" && !methodDef->isStatic &&
                !node->baseClass.empty() && !hasTerminator()) {
                bool might = false;
                for (auto& stmt : methodDef->body)
                    if (stmtMightCallSuper(stmt.get())) { might = true; break; }
                if (!might)
                    builder_.createCall("ts_throw_super_not_called", {},
                                        HIRType::makeAny());
            }

            // Add implicit return if no terminator
            if (!hasTerminator()) {
                builder_.createReturnVoid();
            }

            popScope();

            // Restore saved function
            currentFunction_ = savedFunc;
            currentMethodIsStatic_ = savedMethodStatic;
    flsScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register method in the class. Static `constructor` is a
            // static method that happens to be named "constructor" — NOT
            // the class's instance constructor.
            HIRFunction* funcPtr = func.get();
            if (isComputedName) {
                auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(methodDef->nameNode.get());
                bool isMethod = !methodDef->isGetter && !methodDef->isSetter;
                hirClass->computedAccessors.push_back(
                    {cpn ? cpn->expression.get() : nullptr, funcPtr,
                     methodDef->isSetter, methodDef->isStatic, isMethod,
                     /*moduleLevelBody=*/false});  // class EXPRESSION: ca.func lowered with module vars bound -> correct
            } else if (methodDef->name == "constructor" && !methodDef->isStatic) {
                hirClass->constructor = funcPtr;
            } else if (methodDef->isStatic) {
                // Use methodKey so static accessors get the
                // __getter_<name> / __setter_<name> prefix needed for
                // runtime accessor dispatch on the constructor.
                hirClass->staticMethods[methodKey] = funcPtr;
            } else {
                // Use methodKey for registration (includes __getter_/__setter_ prefix for accessors)
                hirClass->methods[methodKey] = funcPtr;
                // Add to vtable for virtual dispatch
                hirClass->vtable.push_back({methodKey, funcPtr});
            }

            // Add function to module
            module_->functions.push_back(std::move(func));
        }
    }

    // If no explicit constructor was defined, but we have property initializers,
    // generate a default constructor to initialize them
    if (!hirClass->constructor) {
        // Check if there are any property initializers
        bool hasPropertyInitializers = false;
        for (auto& memberPtr : node->members) {
            if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                if (!propDef->isStatic && propDef->initializer) {
                    hasPropertyInitializers = true;
                    break;
                }
            }
        }

        // Always emit a default constructor so the class identifier
        // resolves to a real function value in untyped JS mode. Without
        // a constructor function, `typeof E` and `E.prototype` collapse
        // to undefined because visitIdentifier has nothing to load.
        // The body still calls super() when there is a base class and
        // initializes property defaults when present.
        bool needsDefaultConstructor = true;
        (void)hasPropertyInitializers;

        if (needsDefaultConstructor) {
            std::string ctorName = className + "_constructor";
            auto defaultCtor = std::make_unique<HIRFunction>(ctorName);
            {
                // .name of the (default) constructor = the class name, or the
                // inferred binding name for an anonymous class expression.
                std::string cn = node->name.empty() ? pendingClosureDisplayName_ : node->name;
                if (!cn.empty()) defaultCtor->displayName = cn;
            }

            // 'this' is the first parameter
            defaultCtor->params.push_back({"this", HIRType::makeObject()});
            defaultCtor->nextValueId = 1;

            // Create entry block
            HIRBlock* ctorBlock = defaultCtor->createBlock("entry");
            HIRFunction* savedFunc = currentFunction_;
            bool savedMethodStatic = currentMethodIsStatic_;
            currentMethodIsStatic_ = false;  // synthesized default ctor
    // A nested function body starts OUTSIDE any enclosing try/with:
    // tryDepth_/withDepth_ are lowering-state of the OUTER function. A
    // `return` in a closure defined inside a try{} otherwise emits a
    // PopHandler for a handler the closure never pushed — at runtime it
    // popped the CALLER's exception handler (found via the Promise
    // combinator spec-path whose reject-handler vanished).
    // RAII scope (SMELL-002): saves/resets ALL per-function lowering
    // state. This site previously saved only try/with fields — nested
    // bodies saw the parent loop/label/break stacks and pending
    // captures (latent leak from the audit).
    std::optional<FunctionLoweringScope> flsScope{std::in_place, *this};
            currentFunction_ = defaultCtor.get();
            builder_.setInsertPoint(ctorBlock);
            currentBlock_ = ctorBlock;
            pushScope();

            // Define 'this' in scope
            auto thisValue = std::make_shared<HIRValue>(0, HIRType::makeObject(), "this");
            defineVariable("this", thisValue);

            // Call super() if we have a base class
            if (hirClass->baseClass && hirClass->baseClass->constructor) {
                std::vector<std::shared_ptr<HIRValue>> superArgs;
                superArgs.push_back(thisValue);
                builder_.createCall(hirClass->baseClass->constructor->name, superArgs, HIRType::makeVoid());
            }

            // Initialize property defaults. Every declared instance
            // field is installed on `this`, with `undefined` for
            // fields without initializers — matches ECMA-262 15.7.
            for (auto& memberPtr : node->members) {
                if (auto* propDef = dynamic_cast<ast::PropertyDefinition*>(memberPtr.get())) {
                    if (!propDef->isStatic) {
                        std::shared_ptr<HIRValue> initVal;
                        if (propDef->initializer) {
                            {
                                        // Field-initializer eval context
                                        // (ES ClassFieldDefinition: eval code
                                        // containing 'arguments' -> Syntax-
                                        // Error). flags bit3; owner check
                                        // keeps nested fn bodies plain.
                                        int savedAEF = activeEvalFlags_;
                                        HIRFunction* savedAEO = evalFlagsOwner_;
                                        activeEvalFlags_ |= 12;  // bit2 strict + bit3 field-init
                                        evalFlagsOwner_ = currentFunction_;
                                        initVal = lowerExpression(propDef->initializer.get());
                                        activeEvalFlags_ = savedAEF;
                                        evalFlagsOwner_ = savedAEO;
                                    }
                        } else {
                            initVal = builder_.createConstUndefined();
                        }
                        emitInstanceFieldSet(thisValue, propDef, initVal);
                    }
                }
            }

            // Return void
            builder_.createReturnVoid();

            popScope();
            currentFunction_ = savedFunc;
            currentMethodIsStatic_ = savedMethodStatic;
    flsScope.reset();  // restore ALL lowering state (SMELL-002 RAII scope)
            if (savedFunc) {
                auto* savedBlock = savedFunc->getEntryBlock();
                builder_.setInsertPoint(savedBlock);
                currentBlock_ = savedBlock;
            }

            // Register the default constructor
            hirClass->constructor = defaultCtor.get();
            hirClass->hasSyntheticCtor = true;
            module_->functions.push_back(std::move(defaultCtor));
        }
    }

    // Restore the insert point to what it was before processing methods
    currentFunction_ = savedFuncBeforeMethods;
    currentBlock_ = savedBlockBeforeMethods;
    if (savedBlockBeforeMethods) {
        builder_.setInsertPoint(savedBlockBeforeMethods);
    }

    // Restore class context
    if (!privateClassStack_.empty()) privateClassStack_.pop_back();
    strictCode_ = savedStrictCode;
    currentClass_ = savedClass;

    // Store the generated class name for variable tracking (used by visitVariableDeclaration)
    lastGeneratedClassName_ = className;

    // Phase 9c-i: if invoked from the pre-scan in pass 1 of lower() — when
    // currentFunction_ is null — there's no insert point to emit the value
    // setup into. Defer prototype install to user_main entry instead;
    // visitVariableDeclaration is NOT guaranteed to re-visit the node
    // because the Monomorphizer drops top-level let-decl statements
    // from the spec body when they have a class-expression initializer,
    // so the cache-fast-path's IR emission never happens.
    if (!currentFunction_) {
        // Every class-expression needs the deferred prototype init — even an
        // empty body (`const A = class {}`) needs A.prototype, the
        // constructor backref, and `extends` linkage for instanceof.
        bool already = false;
        for (auto* c : deferredClassPrototypes_) if (c == hirClass) { already = true; break; }
        if (!already) deferredClassPrototypes_.push_back(hirClass);
        return;
    }

    // The result of a class expression is a reference to the class constructor
    // We use LoadFunction to get the constructor pointer
    if (hirClass->constructor) {
        lastValue_ = builder_.createLoadFunction(hirClass->constructor->name);
    } else {
        // If no explicit constructor, load the implicit default constructor
        // For now, just return a pointer to the class (the runtime will handle allocation)
        lastValue_ = builder_.createLoadFunction(className + "_constructor");
    }

    // ECMA-262 14.6.13 ClassDefinitionEvaluation: the class expression's VALUE
    // is the constructor F. Capture it now — emitComputedAccessorInstalls below
    // lowers each computed key via lowerExpression, which overwrites lastValue_
    // with the key string. Without restoring it, an inline
    // `(class { [k](){} }).prototype` read `.prototype` off that string.
    auto ctorResult = lastValue_;

    // Set up prototype object with instance methods for dynamic dispatch.
    // This is critical for untyped JS classes (e.g. npm modules) where method
    // calls go through ts_object_get_property -> prototype chain lookup.
    // Build the prototype when there are instance methods OR computed-name
    // accessors (the latter aren't in the static `methods` map).
    {
        auto ctorVal = ctorResult;

        // Create prototype TsMap — always, even for an empty class body, so
        // `A.prototype` / constructor backref / instanceof-walk all hold.
        auto proto = builder_.createCall("ts_object_create_empty", {}, HIRType::makeAny());

        // Populate prototype with instance methods
        for (auto& [methodKey, methodFunc] : hirClass->methods) {
            if (!methodFunc) continue;  // skip abstract methods

            // Load the method as a closure (LoadFunction wraps in TsClosure)
            auto methodClosure = builder_.createLoadFunction(completeMethodSymbol(hirClass, methodKey, methodFunc));

            // Store on prototype: proto.methodName = closure (non-enumerable,
            // matching a class DECLARATION — createSetPropStatic made class-
            // expression methods wrongly enumerable). methodKey already has the
            // __getter_/__setter_ prefix for accessors.
            installClassMember(proto, qualifyPrivateMemberKey(methodKey, hirClass->name), methodClosure);
        }

        // Set constructor.prototype = proto
        builder_.createSetPropStatic(ctorVal, "prototype", proto);
        // proto.constructor = ctor (non-enumerable backref) — the decl path adds
        // this via emitDeferredStaticInits; the expr trailer was omitting it.
        installClassMember(proto, "constructor", ctorVal);

        // Computed-name accessors install inline here (the prototype is rebuilt
        // at this point, which would clobber a deferred install).
        emitComputedAccessorInstalls(hirClass, proto, ctorVal);
        // `extends` linkage (ES 15.7.14): C.prototype.[[Proto]] =
        // Base.prototype (user class or builtin). Mirrors
        // emitDeferredStaticInits — the trailer rebuild would otherwise
        // drop the chain for in-function class expressions.
        if (hirClass->baseClass) {
            std::string baseCtorName = hirClass->baseClass->constructor
                ? hirClass->baseClass->constructor->name
                : hirClass->baseClass->name + "_constructor";
            auto baseCtorVal = builder_.createLoadFunction(baseCtorName);
            auto basePropName = builder_.createConstString("prototype");
            auto baseProtoVal = builder_.createCall("ts_object_get_dynamic",
                {baseCtorVal, basePropName}, HIRType::makeAny());
            builder_.createCall("ts_object_setPrototypeOf",
                {proto, baseProtoVal}, HIRType::makeVoid());
            builder_.createCall("ts_object_setPrototypeOf",
                {ctorVal, baseCtorVal}, HIRType::makeVoid());
        } else if (!hirClass->baseBuiltinName.empty()) {
            auto baseNameC = builder_.createConstString(hirClass->baseBuiltinName);
            builder_.createCall("ts_class_link_builtin_base",
                {ctorVal, proto, baseNameC}, HIRType::makeVoid());
        }
    }
    // Install static methods on the constructor itself so dynamic-dispatch
    // access like `F.method()` resolves correctly when `F` is a class-
    // expression-bound variable. visitCallExpression's Case 3 only fires
    // for class-name identifiers tracked in module_->classes, not for
    // class-expression variables.
    for (auto& [methodName, methodFunc] : hirClass->staticMethods) {
        if (!methodFunc) continue;
        auto methodClosure = builder_.createLoadFunction(completeMethodSymbol(hirClass, methodName, methodFunc, /*isStatic=*/true));
        installClassMember(ctorResult, qualifyPrivateMemberKey(methodName, hirClass->name), methodClosure);  // non-enumerable
    }
    // Restore the class-expression value (a computed-key install above may have
    // overwritten lastValue_ with the key string).
    lastValue_ = ctorResult;
}

}  // namespace ts::hir
