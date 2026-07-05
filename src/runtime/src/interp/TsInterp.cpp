// TsInterp.cpp — tree-walking interpreter for eval'd / Function-constructed
// code (EVAL-001 Phase 1). Walks the ast::* nodes produced by the runtime-
// linked parser (TsParse.cpp) and delegates EVERY JavaScript operation to the
// same extern "C" ts_* runtime ABI the AOT codegen emits — the walker adds
// only control flow and scope plumbing, never a second implementation of
// language semantics.
//
// Design invariants (see docs/tickets/EVAL-001-treewalker-eval.md §9):
//  - Values are nanboxed TsValue* throughout, exactly like AOT code.
//  - Environment records are TsMap objects (GC-visible object graph; no
//    bespoke rooting). The parent link and metadata live under reserved keys
//    that start with '\x01' (impossible in an identifier). Lookups use
//    TsMap::Get/Has directly — own-properties only, so a fresh map with a
//    null prototype can never leak Object.prototype pollution into a scope.
//  - The C++ recursion stack is the VM stack. The GC conservatively scans
//    the native stack (TsGC.cpp gc_push_conservative_stack_roots), so
//    TsValue* locals in walker frames are roots automatically.
//  - Exceptions NEVER unwind through walker frames via longjmp. Every ts_*
//    call that can throw runs under a leaf setjmp guard (guard frames hold
//    no destructor-owning locals); a caught throw becomes a Thrown
//    completion that propagates by ordinary C++ returns. At the boundary
//    back into native/AOT code (trampoline exit, eval entry) a pending
//    Thrown completion is re-raised with ts_throw from a frame with no
//    destructor-owning locals (the longjmp-stdstring rule).
//  - The AST is transient plain-heap C++ owned by parse handles that are
//    registered here and never freed while the program runs: interpreted
//    closures keep raw pointers into it.
//
// Deferred (throws EvalError naming the construct): classes, generators,
// async/await, destructuring patterns, for-in/for-of, spread, getters/
// setters and computed keys in object literals, tagged templates, regex
// literals, `arguments`, new.target.

#include "../../include/TsObject.h"
#include "../../include/TsRuntime.h"
#include "../../include/TsClosure.h"
#include "../../include/TsCell.h"
#include "../../include/TsMap.h"
#include "../../include/TsArray.h"
#include "../../include/TsString.h"
#include "../../include/TsError.h"
#include "../../include/TsNanBox.h"
#include "../../../compiler/ast/AstNodes.h"

#include <setjmp.h>
#include <cstring>
#include <string>
#include <vector>
#include <set>

// ---------------------------------------------------------------------------
// Runtime ABI decls not exported through headers we can include cleanly.
// ---------------------------------------------------------------------------
extern "C" {
    void* ts_parse_program(const char* source, const char* file_name, int as_module);
    const char* ts_parse_error(void* handle);
    void* ts_parse_get_program(void* handle);
    void ts_parse_free(void* handle);

    void* ts_get_call_this();
    void* ts_get_new_target();
    void* ts_set_new_target(void* v);
    bool ts_instanceof_dynamic(TsValue* obj, TsValue* constructor);
    void* ts_to_string_spec(TsValue* val);
    bool ts_is_callable(void* val);
    void* ts_string_create(const char* str);
    void* ts_string_concat(void* a, void* b);
    bool ts_string_eq(void* a, void* b);

    // Runtime globals (extern "C" definitions live in TsObject.cpp).
    extern TsValue* globalThis;
    extern TsValue* Object;

    void ts_gc_register_root(void** location);
    void ts_closure_set_not_constructable(TsClosure* closure);
    void* ts_interp_global_ctor_by_name(const char* n);

    TsValue* ts_iterator_get(TsValue* iterable);
    TsValue* ts_iterator_next(TsValue* iterator, TsValue* value);
    void ts_iterator_close(TsValue* iter);
    void* ts_object_for_in_keys(void* obj);
    void* ts_regexp_create(void* pattern, void* flags);
    uint8_t ts_integrity_get(void* raw);
    TsValue* ts_object_getOwnPropertyDescriptor(TsValue* obj, TsValue* prop);
}

namespace {

using ast::Expression;
using ast::Statement;

// --- value shorthands -------------------------------------------------------

inline TsValue* jsUndefined() { return (TsValue*)(uintptr_t)NANBOX_UNDEFINED; }
inline TsValue* jsNull()      { return (TsValue*)(uintptr_t)NANBOX_NULL; }
inline TsValue* jsBool(bool b){ return (TsValue*)(uintptr_t)(b ? NANBOX_TRUE : NANBOX_FALSE); }

inline TsValue* boxStr(const std::string& s) {
    return ts_value_make_string(ts_string_create(s.c_str()));
}
inline TsValue* boxObj(void* o) { return ts_value_make_object(o); }

// --- completion records ------------------------------------------------------

struct Cpl {
    enum K { Normal, Ret, Brk, Cont, Thrown } k = Normal;
    TsValue* v = nullptr;   // Normal: statement/expression value; Ret: return
                            // value; Thrown: the exception (boxed error)
    std::string label;      // Brk/Cont label ("" = unlabeled)
};

inline Cpl normal(TsValue* v = nullptr) { Cpl c; c.k = Cpl::Normal; c.v = v; return c; }
inline Cpl thrown(TsValue* ex)          { Cpl c; c.k = Cpl::Thrown; c.v = ex; return c; }
inline bool isAbrupt(const Cpl& c)      { return c.k != Cpl::Normal; }

Cpl throwTyped(const char* ctor, const std::string& msg) {
    return thrown((TsValue*)ts_error_create_typed(ctor, msg.c_str()));
}
Cpl unsupported(const std::string& what) {
    return throwTyped("EvalError", "eval: unsupported construct: " + what);
}

// --- setjmp leaf guards ------------------------------------------------------
// Each guard frame is trivially destructible (pointers only) so the longjmp
// landing here never has to unwind destructor-owning state in THIS frame,
// and no walker frame is ever crossed by a longjmp (the throw happens inside
// the guarded callee). Returns true on success; false with *ex set on throw.

typedef TsValue* (*OpV)();
typedef TsValue* (*Op1)(TsValue*);
typedef TsValue* (*Op2)(TsValue*, TsValue*);

bool guard1(Op1 op, TsValue* a, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = op(a);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guard2(Op2 op, TsValue* a, TsValue* b, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = op(a, b);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardGet(TsValue* obj, TsValue* key, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_object_get_dynamic(obj, key);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardSet(TsValue* obj, TsValue* key, TsValue* val, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        ts_object_set_dynamic(obj, key, val);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardCall(TsValue* fn, TsValue* thisV, int argc, TsValue** argv,
               TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_function_call_with_this(fn, thisV, argc, argv);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardConstruct(TsValue* fn, TsValue* argsArr, TsValue** out, TsValue** ex) {
    // ts_construct_apply does not set [[NewTarget]]; set it to the constructor
    // for the duration of the call so new.target resolves inside the ctor.
    void* prevNT = ts_set_new_target(fn);
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_construct_apply(fn, argsArr);
        ts_pop_exception_handler();
        ts_set_new_target(prevNT);
        return true;
    }
    ts_set_new_target(prevNT);
    *ex = ts_get_exception();
    return false;
}

bool guardHas(TsValue* obj, TsValue* key, bool* out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_object_has_prop(obj, key);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardDelete(TsValue* obj, TsValue* key, bool* out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_object_delete_prop(obj, key);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardToString(TsValue* v, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_value_make_string(ts_to_string_spec(v));
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardIterGet(TsValue* iterable, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_iterator_get(iterable);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardIterNext(TsValue* iter, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_iterator_next(iter, (TsValue*)(uintptr_t)NANBOX_UNDEFINED);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

// IteratorClose on abrupt exits is best-effort: swallow secondary throws.
void iterCloseQuiet(TsValue* iter) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        ts_iterator_close(iter);
        ts_pop_exception_handler();
    }
    // landed: ts_throw already popped the handler; drop the exception
}

bool guardForInKeys(TsValue* obj, TsValue** out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        void* raw = ts_value_get_object(obj);
        *out = ts_value_make_object(ts_object_for_in_keys(raw ? raw : (void*)obj));
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

bool guardInstanceof(TsValue* a, TsValue* b, bool* out, TsValue** ex) {
    void* buf = ts_push_exception_handler();
    if (setjmp(*(jmp_buf*)buf) == 0) {
        *out = ts_instanceof_dynamic(a, b);
        ts_pop_exception_handler();
        return true;
    }
    *ex = ts_get_exception();
    return false;
}

// Convenience: run a binary dispatcher under guard, producing a completion.
Cpl op2(Op2 op, TsValue* a, TsValue* b) {
    TsValue* out = nullptr; TsValue* ex = nullptr;
    if (guard2(op, a, b, &out, &ex)) return normal(out);
    return thrown(ex);
}
Cpl op1(Op1 op, TsValue* a) {
    TsValue* out = nullptr; TsValue* ex = nullptr;
    if (guard1(op, a, &out, &ex)) return normal(out);
    return thrown(ex);
}

// --- environment records ------------------------------------------------------
// One TsMap per scope. Reserved keys (start with '\x01'):
//   "\x01p"        parent env (boxed TsMap), absent at chain root
//   "\x01f"        truthy => function/var scope (var + function-decl target)
//   "\x01g"        truthy => var-declarations go to globalThis (indirect eval)
//   "\x01w"        `with` object (boxed): consult before own bindings
//   "\x01c:"+name  truthy => binding is const
// A binding in TDZ holds the g_tdzMarker object (a unique TsMap, compared by
// identity). NANBOX_TDZ itself can't round-trip TsMap's TaggedValue storage.

TsMap* g_tdzMarker = nullptr;   // lazily created, GC-rooted

TsMap* tdzMarker() {
    if (!g_tdzMarker) {
        g_tdzMarker = (TsMap*)ts_map_create();
        ts_gc_register_root((void**)&g_tdzMarker);
    }
    return g_tdzMarker;
}

inline TsValue key(const char* s) { // TaggedValue string key for TsMap
    return nanbox_to_tagged(boxStr(s));
}
inline TsValue key(const std::string& s) { return key(s.c_str()); }

TsMap* envNew(TsMap* parent, bool fnScope) {
    TsMap* m = (TsMap*)ts_map_create();
    if (parent) m->Set(key("\x01p"), nanbox_to_tagged(boxObj(parent)));
    if (fnScope) m->Set(key("\x01f"), nanbox_to_tagged(jsBool(true)));
    return m;
}

TsMap* envParent(TsMap* env) {
    if (!env->Has(key("\x01p"))) return nullptr;
    TsValue* v = nanbox_from_tagged(env->Get(key("\x01p")));
    return (TsMap*)ts_value_get_object(v);
}

TsValue* envWithObject(TsMap* env) {
    if (!env->Has(key("\x01w"))) return nullptr;
    return nanbox_from_tagged(env->Get(key("\x01w")));
}

bool envIsGlobalVarScope(TsMap* env) { return env->Has(key("\x01g")); }
bool envIsFnScope(TsMap* env) {
    return env->Has(key("\x01f")) || env->Has(key("\x01g"));
}

void envDefine(TsMap* env, const std::string& name, TsValue* v, bool isConst) {
    env->Set(key(name), nanbox_to_tagged(v));
    if (isConst) env->Set(key("\x01c:" + name), nanbox_to_tagged(jsBool(true)));
}

bool envHasOwn(TsMap* env, const std::string& name) {
    return env->Has(key(name));
}
TsValue* envGetOwn(TsMap* env, const std::string& name) {
    return nanbox_from_tagged(env->Get(key(name)));
}
bool envIsConst(TsMap* env, const std::string& name) {
    return env->Has(key("\x01c:" + name));
}
bool isTdz(TsValue* v) {
    return ts_value_get_object(v) == (void*)tdzMarker();
}

// Find the nearest function-scope env for var/function-decl targeting.
TsMap* envVarTarget(TsMap* env) {
    TsMap* e = env;
    while (e && !envIsFnScope(e)) e = envParent(e);
    return e ? e : env;
}

// Look up a reserved key (e.g. "\x01h" home object, "\x01sc" super ctor) in the
// nearest enclosing env that carries it. Returns nullptr if absent.
TsValue* envLookupReserved(TsMap* env, const char* rk) {
    for (TsMap* e = env; e; e = envParent(e))
        if (e->Has(key(rk))) return nanbox_from_tagged(e->Get(key(rk)));
    return nullptr;
}

// --- interpreted function data -------------------------------------------------

struct InterpFn {
    std::vector<std::unique_ptr<ast::Parameter>>* params = nullptr;
    std::vector<ast::StmtPtr>* body = nullptr;   // block body (borrowed from AST)
    Expression* exprBody = nullptr;              // arrow concise body
    std::string name;
    bool isArrow = false;
    bool strict = false;
    // Class support (all null/false for ordinary functions):
    TsValue* homeObject = nullptr;   // [[HomeObject]] for super.prop lookup
    TsValue* superCtor = nullptr;    // base constructor, for super(...) in a ctor
    bool isCtor = false;             // this InterpFn is a class constructor
    bool isDerivedCtor = false;      // derived class ctor (needs super())
    bool isImplicitCtor = false;     // synthesized default constructor
    // Instance field initializers, run at construction (borrowed from AST):
    std::vector<ast::PropertyDefinition*>* fields = nullptr;
};

// Registered function data + parse handles live for the program lifetime:
// interpreted closures hold raw AST pointers. Plain heap, never GC memory.
std::vector<InterpFn*>& fnRegistry() {
    static std::vector<InterpFn*> r;
    return r;
}
void retainParseHandle(void* h) {
    static std::vector<void*> handles;
    handles.push_back(h);
}

// Forward decls
Cpl evalExpr(Expression* e, TsMap* env, TsValue* thisV, bool strict);
Cpl execStmts(std::vector<ast::StmtPtr>& stmts, TsMap* env, TsValue* thisV, bool strict);
Cpl execStmt(Statement* s, TsMap* env, TsValue* thisV, bool strict);
TsValue* makeInterpClosure(InterpFn* fd, TsMap* env, TsValue* thisV);
enum class BindMode { Let, Const, Var, Param };
Cpl bindPattern(ast::Node* target, TsValue* v, TsMap* env, TsValue* thisV,
                bool strict, BindMode mode);
Cpl readIdent(TsMap* env, const std::string& name, bool forTypeof);
Cpl evalClass(const std::string& className, const std::string& baseClassName,
              std::vector<ast::NodePtr>& members, TsMap* env, TsValue* thisV,
              bool strict);
Cpl initInstanceFields(InterpFn* fd, TsMap* env, TsValue* thisV, bool strict);

// --- strictness ---------------------------------------------------------------

bool bodyHasUseStrict(std::vector<ast::StmtPtr>& body) {
    // Scan the directive prologue: leading ExpressionStatements whose
    // expression is a StringLiteral. Stop at the first non-directive.
    for (auto& s : body) {
        auto* es = dynamic_cast<ast::ExpressionStatement*>(s.get());
        if (!es) return false;
        auto* sl = dynamic_cast<ast::StringLiteral*>(es->expression.get());
        if (!sl) return false;
        if (sl->value == "use strict") return true;
        // other directive ("use asm", ...) — keep scanning
    }
    return false;
}

// --- identifier resolution -------------------------------------------------------

// Result codes for identifier lookup.
enum class LookupResult { Found, Tdz, NotFound, Threw };

LookupResult envLookup(TsMap* env, const std::string& name, TsValue** out,
                       TsValue** ex) {
    for (TsMap* e = env; e; e = envParent(e)) {
        if (TsValue* wobj = envWithObject(e)) {
            bool has = false;
            if (!guardHas(wobj, boxStr(name), &has, ex)) return LookupResult::Threw;
            if (has) {
                if (!guardGet(wobj, boxStr(name), out, ex)) return LookupResult::Threw;
                return LookupResult::Found;
            }
        }
        if (envHasOwn(e, name)) {
            TsValue* v = envGetOwn(e, name);
            if (isTdz(v)) return LookupResult::Tdz;
            *out = v;
            return LookupResult::Found;
        }
    }
    // Global object fallback.
    bool has = false;
    if (!guardHas(globalThis, boxStr(name), &has, ex)) return LookupResult::Threw;
    if (has) {
        if (!guardGet(globalThis, boxStr(name), out, ex)) return LookupResult::Threw;
        // Builtin-constructor globalThis entries hold only their NAME STRING
        // (the real constructors are first-class cached singletons — the kTA
        // trap). Swap in the callable constructor when we read the marker.
        void* sraw = *out ? ts_value_get_string(*out) : nullptr;
        if (sraw && ts_string_eq(sraw, ts_string_create(name.c_str()))) {
            if (void* ctor = ts_interp_global_ctor_by_name(name.c_str()))
                *out = (TsValue*)ctor;
        }
        return LookupResult::Found;
    }
    if (void* ctor = ts_interp_global_ctor_by_name(name.c_str())) {
        *out = (TsValue*)ctor;
        return LookupResult::Found;
    }
    return LookupResult::NotFound;
}

Cpl readIdent(TsMap* env, const std::string& name, bool forTypeof) {
    TsValue* out = nullptr; TsValue* ex = nullptr;
    switch (envLookup(env, name, &out, &ex)) {
    case LookupResult::Found:   return normal(out);
    case LookupResult::Threw:   return thrown(ex);
    case LookupResult::Tdz:
        return throwTyped("ReferenceError",
                          "Cannot access '" + name + "' before initialization");
    case LookupResult::NotFound:
        if (forTypeof) return normal(jsUndefined());
        return throwTyped("ReferenceError", name + " is not defined");
    }
    return normal(jsUndefined());
}

Cpl assignIdent(TsMap* env, const std::string& name, TsValue* v, bool strict) {
    for (TsMap* e = env; e; e = envParent(e)) {
        if (TsValue* wobj = envWithObject(e)) {
            bool has = false; TsValue* ex = nullptr;
            if (!guardHas(wobj, boxStr(name), &has, &ex)) return thrown(ex);
            if (has) {
                if (!guardSet(wobj, boxStr(name), v, &ex)) return thrown(ex);
                return normal(v);
            }
        }
        if (envHasOwn(e, name)) {
            if (isTdz(envGetOwn(e, name)))
                return throwTyped("ReferenceError",
                                  "Cannot access '" + name + "' before initialization");
            if (envIsConst(e, name))
                return throwTyped("TypeError", "Assignment to constant variable.");
            e->Set(key(name), nanbox_to_tagged(v));
            return normal(v);
        }
    }
    // Unresolved: sloppy creates a global; strict throws (ES 6.2.5.6).
    if (strict) {
        bool has = false; TsValue* ex = nullptr;
        if (!guardHas(globalThis, boxStr(name), &has, &ex)) return thrown(ex);
        if (!has) return throwTyped("ReferenceError", name + " is not defined");
    }
    TsValue* ex = nullptr;
    if (!guardSet(globalThis, boxStr(name), v, &ex)) return thrown(ex);
    return normal(v);
}

// --- hoisting -------------------------------------------------------------------
// Pre-pass over a function/program body: var declarations bind undefined in
// the var-scope env; function declarations bind their closure (created over
// `env`). Descends into nested statements but NOT nested functions.

void hoistCollect(Statement* s, std::vector<std::string>& vars,
                  std::vector<ast::FunctionDeclaration*>& fns) {
    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(s)) {
        if (vd->varKind == ast::VarKind::Var) {
            if (auto* id = dynamic_cast<ast::Identifier*>(vd->name.get()))
                vars.push_back(id->name);
        }
        return;
    }
    if (dynamic_cast<ast::FunctionDeclaration*>(s)) {
        // A function declaration is NOT a var-scope binding here. Direct-body
        // fns are collected separately (collectTopLevelFns); block-nested fns
        // are block-scoped + Annex-B promoted (see annexCollectPromoted). Do
        // not descend into the nested function body (own scope).
        return;
    }
    if (auto* b = dynamic_cast<ast::BlockStatement*>(s)) {
        for (auto& st : b->statements) hoistCollect(st.get(), vars, fns);
        return;
    }
    if (auto* i = dynamic_cast<ast::IfStatement*>(s)) {
        if (i->thenStatement) hoistCollect(i->thenStatement.get(), vars, fns);
        if (i->elseStatement) hoistCollect(i->elseStatement.get(), vars, fns);
        return;
    }
    if (auto* w = dynamic_cast<ast::WhileStatement*>(s)) {
        if (w->body) hoistCollect(w->body.get(), vars, fns);
        return;
    }
    if (auto* f = dynamic_cast<ast::ForStatement*>(s)) {
        if (f->initializer) hoistCollect(f->initializer.get(), vars, fns);
        if (f->body) hoistCollect(f->body.get(), vars, fns);
        return;
    }
    if (auto* fo = dynamic_cast<ast::ForOfStatement*>(s)) {
        if (fo->initializer) hoistCollect(fo->initializer.get(), vars, fns);
        if (fo->body) hoistCollect(fo->body.get(), vars, fns);
        return;
    }
    if (auto* fi = dynamic_cast<ast::ForInStatement*>(s)) {
        if (fi->initializer) hoistCollect(fi->initializer.get(), vars, fns);
        if (fi->body) hoistCollect(fi->body.get(), vars, fns);
        return;
    }
    if (auto* t = dynamic_cast<ast::TryStatement*>(s)) {
        for (auto& st : t->tryBlock) hoistCollect(st.get(), vars, fns);
        if (t->catchClause)
            for (auto& st : t->catchClause->block) hoistCollect(st.get(), vars, fns);
        for (auto& st : t->finallyBlock) hoistCollect(st.get(), vars, fns);
        return;
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(s)) {
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get()))
                for (auto& st : cc->statements) hoistCollect(st.get(), vars, fns);
            else if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get()))
                for (auto& st : dc->statements) hoistCollect(st.get(), vars, fns);
        }
        return;
    }
    if (auto* l = dynamic_cast<ast::LabeledStatement*>(s)) {
        if (l->statement) hoistCollect(l->statement.get(), vars, fns);
        return;
    }
}

// Unwrap labels (transparent for hoisting/scoping purposes).
inline Statement* unlabel(Statement* s) {
    while (auto* l = dynamic_cast<ast::LabeledStatement*>(s)) s = l->statement.get();
    return s;
}

// Direct-body-level function declarations (var-scoped in a function/eval body).
// Synthetic blocks and labels are transparent; real blocks are NOT descended.
void collectTopLevelFns(std::vector<ast::StmtPtr>& body,
                        std::vector<ast::FunctionDeclaration*>& fns) {
    for (auto& sp : body) {
        Statement* s = unlabel(sp.get());
        if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(s)) { fns.push_back(fd); continue; }
        if (auto* b = dynamic_cast<ast::BlockStatement*>(s))
            if (b->isSynthetic) collectTopLevelFns(b->statements, fns);
    }
}

// --- Annex B.3.3: block-scoped function declarations in (sloppy) eval code ---
// A FunctionDeclaration nested in a block / if-branch / loop body / switch
// clause / try-catch-finally block is block-scoped (R1) and, unless shadowed
// by an enclosing lexical binding of the same name (R4), is "promoted": a var
// binding is pre-created at instantiation and the block-local value is copied
// to the var scope when the declaration statement runs. Strict mode: R1 only.

// Lexical names declared directly in a statement list: let/const/class always,
// and (when this list is a block, not the var-scope top level) block-level
// function declarations too.
void collectLexNamesInList(std::vector<Statement*>& stmts, bool includeFns,
                           std::set<std::string>& out) {
    for (Statement* s0 : stmts) {
        Statement* s = unlabel(s0);
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(s)) {
            if (vd->varKind != ast::VarKind::Var)
                if (auto* id = dynamic_cast<ast::Identifier*>(vd->name.get()))
                    out.insert(id->name);
        } else if (auto* cd = dynamic_cast<ast::ClassDeclaration*>(s)) {
            if (!cd->name.empty()) out.insert(cd->name);
        } else if (includeFns) {
            if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(s))
                if (!fd->name.empty()) out.insert(fd->name);
        }
    }
}

void annexWalkStmts(std::vector<Statement*> stmts, std::set<std::string> encl,
                    bool atTop, std::set<std::string>& promoted);
void annexDescendScope(Statement* s, const std::set<std::string>& encl,
                       std::set<std::string>& promoted);

// A branch (if then/else): a bare FunctionDeclaration is a block-level candidate
// (Annex B.3.2 synthesized block); otherwise descend as an ordinary scope.
void annexBranchWalk(Statement* s, const std::set<std::string>& encl,
                     std::set<std::string>& promoted) {
    if (!s) return;
    s = unlabel(s);
    if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(s)) {
        if (!fd->name.empty() && !encl.count(fd->name)) promoted.insert(fd->name);
        return;
    }
    annexDescendScope(s, encl, promoted);
}

void annexDescendScope(Statement* s, const std::set<std::string>& encl,
                       std::set<std::string>& promoted) {
    if (!s) return;
    s = unlabel(s);
    if (auto* b = dynamic_cast<ast::BlockStatement*>(s)) {
        std::vector<Statement*> raw;
        for (auto& st : b->statements) raw.push_back(st.get());
        annexWalkStmts(raw, encl, /*atTop*/false, promoted);
        return;
    }
    if (auto* i = dynamic_cast<ast::IfStatement*>(s)) {
        annexBranchWalk(i->thenStatement.get(), encl, promoted);
        annexBranchWalk(i->elseStatement.get(), encl, promoted);
        return;
    }
    auto headName = [](ast::Statement* init, std::set<std::string>& e) {
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(init))
            if (vd->varKind != ast::VarKind::Var)
                if (auto* id = dynamic_cast<ast::Identifier*>(vd->name.get()))
                    e.insert(id->name);
    };
    if (auto* f = dynamic_cast<ast::ForStatement*>(s)) {
        std::set<std::string> e2 = encl;
        if (f->initializer) headName(f->initializer.get(), e2);
        annexDescendScope(f->body.get(), e2, promoted);
        return;
    }
    if (auto* fo = dynamic_cast<ast::ForOfStatement*>(s)) {
        std::set<std::string> e2 = encl;
        if (fo->initializer) headName(fo->initializer.get(), e2);
        annexDescendScope(fo->body.get(), e2, promoted);
        return;
    }
    if (auto* fi = dynamic_cast<ast::ForInStatement*>(s)) {
        std::set<std::string> e2 = encl;
        if (fi->initializer) headName(fi->initializer.get(), e2);
        annexDescendScope(fi->body.get(), e2, promoted);
        return;
    }
    if (auto* w = dynamic_cast<ast::WhileStatement*>(s)) {
        annexDescendScope(w->body.get(), encl, promoted);
        return;
    }
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(s)) {
        // All clauses share ONE lexical scope: flatten clause statements into a
        // single list so the union of clause lexicals blocks nested candidates.
        std::vector<Statement*> raw;
        for (auto& cl : sw->clauses) {
            if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get()))
                for (auto& st : cc->statements) raw.push_back(st.get());
            else if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get()))
                for (auto& st : dc->statements) raw.push_back(st.get());
        }
        annexWalkStmts(raw, encl, /*atTop*/false, promoted);
        return;
    }
    if (auto* t = dynamic_cast<ast::TryStatement*>(s)) {
        std::vector<Statement*> traw;
        for (auto& st : t->tryBlock) traw.push_back(st.get());
        annexWalkStmts(traw, encl, false, promoted);
        if (t->catchClause) {
            std::set<std::string> ce = encl;
            // B.3.5: a SIMPLE identifier catch param does NOT block; a
            // destructuring pattern binds names that DO block.
            if (t->catchClause->variable &&
                !dynamic_cast<ast::Identifier*>(t->catchClause->variable.get())) {
                if (auto* obp = dynamic_cast<ast::ObjectBindingPattern*>(
                        t->catchClause->variable.get()))
                    for (auto& el : obp->elements)
                        if (auto* be = dynamic_cast<ast::BindingElement*>(el.get()))
                            if (auto* id = dynamic_cast<ast::Identifier*>(be->name.get()))
                                ce.insert(id->name);
                if (auto* abp = dynamic_cast<ast::ArrayBindingPattern*>(
                        t->catchClause->variable.get()))
                    for (auto& el : abp->elements)
                        if (auto* be = dynamic_cast<ast::BindingElement*>(el.get()))
                            if (auto* id = dynamic_cast<ast::Identifier*>(be->name.get()))
                                ce.insert(id->name);
            }
            std::vector<Statement*> craw;
            for (auto& st : t->catchClause->block) craw.push_back(st.get());
            annexWalkStmts(craw, ce, false, promoted);
        }
        std::vector<Statement*> fraw;
        for (auto& st : t->finallyBlock) fraw.push_back(st.get());
        annexWalkStmts(fraw, encl, false, promoted);
        return;
    }
}

void annexWalkStmts(std::vector<Statement*> stmts, std::set<std::string> encl,
                    bool atTop, std::set<std::string>& promoted) {
    // Direct block-level function declarations promote unless shadowed by an
    // enclosing lexical binding. At the var-scope top level, fns are var-scoped
    // (not block candidates).
    if (!atTop)
        for (Statement* s0 : stmts) {
            Statement* s = unlabel(s0);
            if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(s))
                if (!fd->name.empty() && !encl.count(fd->name)) promoted.insert(fd->name);
        }
    // Lexical names introduced at this level are visible to nested scopes.
    std::set<std::string> here = encl;
    collectLexNamesInList(stmts, /*includeFns*/!atTop, here);
    for (Statement* s0 : stmts) {
        Statement* s = unlabel(s0);
        // Synthetic blocks add no scope: fold their statements at this level.
        if (auto* b = dynamic_cast<ast::BlockStatement*>(s))
            if (b->isSynthetic) {
                std::vector<Statement*> raw;
                for (auto& st : b->statements) raw.push_back(st.get());
                annexWalkStmts(raw, here, atTop, promoted);
                continue;
            }
        annexDescendScope(s, here, promoted);
    }
}

void annexCollectPromoted(std::vector<ast::StmtPtr>& body,
                          std::set<std::string>& promoted) {
    std::vector<Statement*> raw;
    for (auto& sp : body) raw.push_back(sp.get());
    annexWalkStmts(raw, {}, /*atTop*/true, promoted);
}

// Instantiate block-level function declarations as block-local bindings over
// `benv` (R1). Runs at block entry; labels are transparent.
void instantiateBlockFns(std::vector<ast::StmtPtr>& stmts, TsMap* benv,
                         TsValue* thisV, bool strict);

// Declares hoisted names into the var-scope env (or globalThis for indirect
// sloppy eval). Function declarations are instantiated immediately.
Cpl hoistInto(std::vector<ast::StmtPtr>& body, TsMap* env, TsValue* thisV,
              bool strict) {
    std::vector<std::string> vars;
    std::vector<ast::FunctionDeclaration*> fns;
    for (auto& s : body) hoistCollect(s.get(), vars, fns);
    collectTopLevelFns(body, fns);

    TsMap* target = envVarTarget(env);
    bool toGlobal = envIsGlobalVarScope(target);

    // ES 19.2.1.3 EvalDeclarationInstantiation steps 8/10: on the global
    // record, CanDeclareGlobalVar / CanDeclareGlobalFunction are false for a
    // NEW binding when the global object is non-extensible -> TypeError
    // BEFORE any declaration is instantiated.
    bool globalNonExtensible = false;
    if (toGlobal) {
        // The global object is a TsMap: extensibility is the map's OWN flag
        // (map->PreventExtensions()), NOT the g_obj_integrity side table —
        // that table only covers non-map receivers.
        void* graw = ts_value_get_object(globalThis);
        if (graw) {
            TsMap* gm = dynamic_cast<TsMap*>((TsObject*)graw);
            if (gm) globalNonExtensible = !gm->IsExtensible();
            else    globalNonExtensible = ts_integrity_get(graw) >= 1;
        }
        for (auto& name : vars) {
            bool has = false; TsValue* ex = nullptr;
            if (!guardHas(globalThis, boxStr(name), &has, &ex)) return thrown(ex);
            if (!has && globalNonExtensible)
                return throwTyped("TypeError",
                    "Cannot declare global variable '" + name +
                    "': global object is not extensible");
        }
        for (auto* fd : fns) {
            bool has = false; TsValue* ex = nullptr;
            if (!guardHas(globalThis, boxStr(fd->name), &has, &ex)) return thrown(ex);
            if (!has && globalNonExtensible)
                return throwTyped("TypeError",
                    "Cannot declare global function '" + fd->name +
                    "': global object is not extensible");
            if (has) {
                // ES 9.1.1.4.16 CanDeclareGlobalFunction: an existing OWN
                // property blocks the declaration unless it is configurable,
                // or is a writable+enumerable data property.
                TsValue* desc =
                    ts_object_getOwnPropertyDescriptor(globalThis, boxStr(fd->name));
                if (desc && ts_value_get_object(desc)) {
                    TsValue* cfg = nullptr; TsValue* wr = nullptr;
                    TsValue* en = nullptr;
                    if (!guardGet(desc, boxStr("configurable"), &cfg, &ex)) return thrown(ex);
                    if (!guardGet(desc, boxStr("writable"), &wr, &ex)) return thrown(ex);
                    if (!guardGet(desc, boxStr("enumerable"), &en, &ex)) return thrown(ex);
                    bool configurable = cfg && ts_value_to_bool(cfg);
                    bool dataWritableEnumerable =
                        wr && ts_value_to_bool(wr) && en && ts_value_to_bool(en);
                    if (!configurable && !dataWritableEnumerable)
                        return throwTyped("TypeError",
                            "Cannot declare global function '" + fd->name + "'");
                }
            }
        }
    }

    for (auto& name : vars) {
        if (toGlobal) {
            bool has = false; TsValue* ex = nullptr;
            if (!guardHas(globalThis, boxStr(name), &has, &ex)) return thrown(ex);
            if (!has && !guardSet(globalThis, boxStr(name), jsUndefined(), &ex))
                return thrown(ex);
        } else if (!envHasOwn(target, name)) {
            envDefine(target, name, jsUndefined(), false);
        }
    }
    for (auto* fd : fns) {
        auto* data = new InterpFn();
        data->params = &fd->parameters;
        data->body = &fd->body;
        data->name = fd->name;
        data->strict = strict || bodyHasUseStrict(fd->body);
        fnRegistry().push_back(data);
        TsValue* fn = makeInterpClosure(data, env, thisV);
        if (toGlobal) {
            TsValue* ex = nullptr;
            if (!guardSet(globalThis, boxStr(fd->name), fn, &ex)) return thrown(ex);
        } else {
            envDefine(target, fd->name, fn, false);
        }
    }

    // Annex B.3.3.3: pre-create var bindings for promoted block-level function
    // declarations (sloppy only). The block-local binding + copy at the decl
    // statement are handled at execution time; here we only reserve the var
    // slot (undefined) and mark the name promoted via "\x01x:" on the target.
    if (!strict) {
        std::set<std::string> promoted;
        annexCollectPromoted(body, promoted);
        for (auto& name : promoted) {
            if (toGlobal) {
                // Own-property check (proto chain lies); leave any existing own
                // property (value AND attributes) untouched. Non-extensible +
                // no own property -> skip silently (CanDeclareGlobalVar false).
                TsValue* desc =
                    ts_object_getOwnPropertyDescriptor(globalThis, boxStr(name));
                bool ownExists = desc && ts_value_get_object(desc);
                if (!ownExists) {
                    if (globalNonExtensible) continue;
                    TsValue* ex = nullptr;
                    if (!guardSet(globalThis, boxStr(name), jsUndefined(), &ex))
                        return thrown(ex);
                }
            } else if (!envHasOwn(target, name)) {
                envDefine(target, name, jsUndefined(), false);
            }
            target->Set(key("\x01x:" + name), nanbox_to_tagged(jsBool(true)));
        }
    }
    return normal();
}

// Instantiate one block-level function declaration as a block-local closure
// over `benv` (Annex B R1).
void instantiateBlockFn1(ast::FunctionDeclaration* fd, TsMap* benv,
                         TsValue* thisV, bool strict) {
    if (!fd || fd->name.empty()) return;
    auto* data = new InterpFn();
    data->params = &fd->parameters;
    data->body = &fd->body;
    data->name = fd->name;
    data->strict = strict || bodyHasUseStrict(fd->body);
    fnRegistry().push_back(data);
    envDefine(benv, fd->name, makeInterpClosure(data, benv, thisV), false);
}

// Instantiate all direct block-level fns of a statement list; labels transparent.
void instantiateBlockFns(std::vector<ast::StmtPtr>& stmts, TsMap* benv,
                         TsValue* thisV, bool strict) {
    for (auto& sp : stmts)
        instantiateBlockFn1(dynamic_cast<ast::FunctionDeclaration*>(unlabel(sp.get())),
                            benv, thisV, strict);
}

// Pre-declare block-level let/const as TDZ in the block env.
void predeclareLexical(std::vector<ast::StmtPtr>& stmts, TsMap* env) {
    for (auto& s : stmts) {
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(s.get())) {
            if (vd->varKind != ast::VarKind::Var) {
                if (auto* id = dynamic_cast<ast::Identifier*>(vd->name.get()))
                    env->Set(key(id->name), nanbox_to_tagged(boxObj(tdzMarker())));
            }
        } else if (auto* cd = dynamic_cast<ast::ClassDeclaration*>(s.get())) {
            if (!cd->name.empty())   // class binding is lexical (TDZ until eval'd)
                env->Set(key(cd->name), nanbox_to_tagged(boxObj(tdzMarker())));
        }
    }
}

// --- interpreted closures ---------------------------------------------------------

// Trampoline convention: rest_param_index = 0 packs the caller's ENTIRE argv
// into one TsArray, so ts_call_N invokes func_ptr as
//   FnPad(closure, argsArray, undefined, undefined, undefined)
// `this` arrives via ts_get_call_this() (set by ts_function_call_with_this).

// Re-raise a Thrown completion into native code. Separate noinline frame with
// no destructor-owning locals (longjmp-stdstring rule).
__declspec(noinline) TsValue* interpRethrow(TsValue* ex) {
    ts_throw(ex);
    return nullptr; // unreachable
}

Cpl runFunctionBody(InterpFn* fd, TsMap* defEnv, TsValue* thisV, TsArray* args);

TsValue* interpTramp(void* closurePtr, TsValue* argsArrBoxed, TsValue*, TsValue*, TsValue*) {
    TsClosure* c = (TsClosure*)closurePtr;
    TsMap* env = (TsMap*)ts_value_get_object(ts_cell_get(c->getCell(0)));
    InterpFn* fd = (InterpFn*)(intptr_t)ts_value_get_int(ts_cell_get(c->getCell(1)));

    TsValue* thisV;
    if (fd->isArrow) {
        thisV = ts_cell_get(c->getCell(2));
    } else {
        thisV = (TsValue*)ts_get_call_this();
        // OrdinaryCallBindThis: sloppy callee coerces nullish this to
        // globalThis; strict callee keeps it as-is.
        if (!fd->strict && (!thisV || ts_value_is_nullish(thisV)))
            thisV = globalThis;
    }

    TsArray* args = argsArrBoxed
        ? (TsArray*)ts_value_get_object(argsArrBoxed) : nullptr;

    Cpl r = runFunctionBody(fd, env, thisV, args);
    if (r.k == Cpl::Thrown) return interpRethrow(r.v);
    if (r.k == Cpl::Ret && r.v) return r.v;
    return jsUndefined();
}

TsValue* makeInterpClosure(InterpFn* fd, TsMap* env, TsValue* thisV) {
    TsClosure* c = ts_closure_create((void*)interpTramp, 3);
    ts_closure_init_capture(c, 0, boxObj(env));
    ts_closure_init_capture(c, 1, ts_value_make_int((int64_t)(intptr_t)fd));
    ts_closure_init_capture(c, 2, fd->isArrow && thisV ? thisV : jsUndefined());
    ts_closure_set_rest_index(c, 0);

    // .length: params before the first default/rest (ES 10.2.9).
    int32_t arity = 0;
    if (fd->params) {
        for (auto& p : *fd->params) {
            if (p->isRest || p->initializer) break;
            ++arity;
        }
    }
    ts_closure_set_arity(c, arity);
    if (!fd->name.empty())
        ts_closure_set_name(c, ts_string_create(fd->name.c_str()));

    TsValue* boxed = boxObj(c);
    if (fd->isArrow) {
        ts_closure_set_not_constructable(c);
        ts_closure_set_no_prototype(c);
    } else {
        // .prototype = { constructor: fn }. NO explicit [[Prototype]]: AOT
        // object maps leave prototype null and rely on the dynamic
        // Object.prototype fallback for gets; an explicit SetPrototype makes
        // ts_object_for_in_keys enumerate Object.prototype's methods.
        TsMap* proto = (TsMap*)ts_map_create();
        proto->Set(key("constructor"), nanbox_to_tagged(boxed));
        TsValue* ex2 = nullptr;
        guardSet(boxed, boxStr("prototype"), boxObj(proto), &ex2);
    }
    return boxed;
}

Cpl runFunctionBody(InterpFn* fd, TsMap* defEnv, TsValue* thisV, TsArray* args) {
    TsMap* fenv = envNew(defEnv, /*fnScope*/true);
    bool strict = fd->strict;

    // Class-method super support: expose [[HomeObject]] and the base ctor to
    // the body via reserved env keys, and (for a derived ctor) the field-init
    // hook so the super() call site can run instance fields afterwards.
    if (fd->homeObject) fenv->Set(key("\x01h"), nanbox_to_tagged(fd->homeObject));
    if (fd->superCtor)  fenv->Set(key("\x01sc"), nanbox_to_tagged(fd->superCtor));
    if (fd->isCtor)
        fenv->Set(key("\x01" "cf"), ts_value_make_int((int64_t)(intptr_t)fd));

    // Implicit (synthesized) constructor: derived calls super(...args); then
    // instance fields initialize; the constructor returns `this`.
    if (fd->isCtor && fd->isImplicitCtor) {
        if (fd->isDerivedCtor && fd->superCtor) {
            int64_t argc0 = args ? ts_array_length((void*)args) : 0;
            std::vector<TsValue*> av((size_t)argc0);
            for (int64_t i = 0; i < argc0; i++)
                av[(size_t)i] = ts_array_get_dynamic(boxObj(args), ts_value_make_int(i));
            TsValue* out = nullptr; TsValue* ex = nullptr;
            if (!guardCall(fd->superCtor, thisV, (int)argc0,
                           av.empty() ? nullptr : av.data(), &out, &ex))
                return thrown(ex);
        }
        Cpl fr = initInstanceFields(fd, fenv, thisV, strict);
        if (isAbrupt(fr)) return fr;
        Cpl ret; ret.k = Cpl::Ret; ret.v = thisV; return ret;
    }
    // Base-class explicit ctor: fields initialize before the body runs. (A
    // derived ctor initializes fields right after its super() call — handled
    // at the super() call site.)
    if (fd->isCtor && !fd->isDerivedCtor) {
        Cpl fr = initInstanceFields(fd, fenv, thisV, strict);
        if (isAbrupt(fr)) return fr;
    }

    // Named function expression: the name binds to the function itself in an
    // intermediate immutable binding; approximated as a normal binding here.
    // (Set lazily from the closure the caller boxed — recovered via env chain
    // when needed; skipped in this milestone: recursion resolves through the
    // outer binding for declarations, which covers the common cases.)

    int64_t argc = args ? ts_array_length((void*)args) : 0;
    int64_t argIdx = 0;
    if (fd->params) {
        for (auto& p : *fd->params) {
            auto* id = dynamic_cast<ast::Identifier*>(p->name.get());
            if (p->isRest) {
                TsArray* rest = TsArray::Create((size_t)(argc > argIdx ? argc - argIdx : 0));
                for (int64_t i = argIdx; i < argc; i++) {
                    TsValue* el = ts_array_get_dynamic(boxObj(args), ts_value_make_int(i));
                    ts_array_push_any((void*)rest, el);
                }
                if (id) envDefine(fenv, id->name, boxObj(rest), false);
                else {
                    Cpl bc = bindPattern(p->name.get(), boxObj(rest), fenv, thisV,
                                         strict, BindMode::Param);
                    if (isAbrupt(bc)) return bc;
                }
                break;
            }
            TsValue* v = (argIdx < argc)
                ? ts_array_get_dynamic(boxObj(args), ts_value_make_int(argIdx))
                : jsUndefined();
            argIdx++;
            if (p->initializer && ts_value_is_undefined(v)) {
                Cpl d = evalExpr((Expression*)p->initializer.get(), fenv, thisV, strict);
                if (isAbrupt(d)) return d;
                v = d.v;
            }
            if (id) envDefine(fenv, id->name, v, false);
            else {
                Cpl bc = bindPattern(p->name.get(), v, fenv, thisV, strict, BindMode::Param);
                if (isAbrupt(bc)) return bc;
            }
        }
    }

    if (fd->exprBody) {
        Cpl r = evalExpr(fd->exprBody, fenv, thisV, strict);
        if (isAbrupt(r)) return r;
        Cpl ret; ret.k = Cpl::Ret; ret.v = r.v;
        return ret;
    }

    Cpl h = hoistInto(*fd->body, fenv, thisV, strict);
    if (isAbrupt(h)) return h;
    Cpl r = execStmts(*fd->body, fenv, thisV, strict);
    if (r.k == Cpl::Brk || r.k == Cpl::Cont)
        return throwTyped("SyntaxError", "Illegal break/continue in function body");
    return r;
}

// --- function/arrow expression evaluation ----------------------------------------

InterpFn* fnDataForFunctionExpr(ast::FunctionExpression* fe, bool outerStrict) {
    auto* d = new InterpFn();
    d->params = &fe->parameters;
    d->body = &fe->body;
    d->name = fe->name;
    d->strict = outerStrict || bodyHasUseStrict(fe->body);
    fnRegistry().push_back(d);
    return d;
}

// --- expression evaluation ---------------------------------------------------------

// Evaluates a property key expression to a boxed key value (string/symbol/number).
Cpl evalKey(Expression* e, TsMap* env, TsValue* thisV, bool strict) {
    return evalExpr(e, env, thisV, strict);
}

// Member access target decomposition for assignment / calls.
struct MemberRef {
    TsValue* obj = nullptr;
    TsValue* keyV = nullptr;
};

Cpl evalMemberRef(Expression* e, TsMap* env, TsValue* thisV, bool strict,
                  MemberRef* out) {
    if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(e)) {
        Cpl o = evalExpr(pa->expression.get(), env, thisV, strict);
        if (isAbrupt(o)) return o;
        out->obj = o.v;
        out->keyV = boxStr(pa->name);
        return normal();
    }
    if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(e)) {
        Cpl o = evalExpr(ea->expression.get(), env, thisV, strict);
        if (isAbrupt(o)) return o;
        Cpl k = evalKey(ea->argumentExpression.get(), env, thisV, strict);
        if (isAbrupt(k)) return k;
        out->obj = o.v;
        out->keyV = k.v;
        return normal();
    }
    return unsupported("assignment target " + e->getKind());
}

Cpl evalCall(ast::CallExpression* ce, TsMap* env, TsValue* thisV, bool strict);
Cpl evalBinary(ast::BinaryExpression* be, TsMap* env, TsValue* thisV, bool strict);
Cpl evalAssignment(ast::AssignmentExpression* ae, TsMap* env, TsValue* thisV, bool strict);
Cpl runProgramInEnv(ast::Program* prog, TsMap* env, TsValue* thisV, bool callerStrict);

Cpl evalExpr(Expression* e, TsMap* env, TsValue* thisV, bool strict) {
    if (!e) return normal(jsUndefined());

    if (auto* n = dynamic_cast<ast::NumericLiteral*>(e))
        return normal(ts_value_make_double(n->value));
    if (auto* s = dynamic_cast<ast::StringLiteral*>(e))
        return normal(boxStr(s->value));
    if (auto* b = dynamic_cast<ast::BooleanLiteral*>(e))
        return normal(jsBool(b->value));
    if (dynamic_cast<ast::NullLiteral*>(e))
        return normal(jsNull());
    if (dynamic_cast<ast::UndefinedLiteral*>(e))
        return normal(jsUndefined());

    if (auto* re = dynamic_cast<ast::RegularExpressionLiteral*>(e)) {
        // text is the full literal "/pattern/flags"; split at the LAST '/'.
        const std::string& t = re->text;
        size_t lastSlash = t.rfind('/');
        std::string pat = (t.size() >= 2 && t[0] == '/' && lastSlash > 0)
            ? t.substr(1, lastSlash - 1) : t;
        std::string flags = (lastSlash != std::string::npos && lastSlash + 1 <= t.size())
            ? t.substr(lastSlash + 1) : "";
        void* rx = ts_regexp_create(ts_string_create(pat.c_str()),
                                    ts_string_create(flags.c_str()));
        return normal(boxObj(rx));
    }

    if (auto* id = dynamic_cast<ast::Identifier*>(e)) {
        // The parser encodes `this` as an Identifier named "this".
        if (id->name == "this") return normal(thisV ? thisV : jsUndefined());
        if (id->name == "undefined") return normal(jsUndefined());
        if (id->name == "globalThis") return normal(globalThis);
        return readIdent(env, id->name, /*forTypeof*/false);
    }

    if (dynamic_cast<ast::SuperExpression*>(e))
        return unsupported("super");

    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(e))
        return evalExpr(p->expression.get(), env, thisV, strict);
    if (auto* a = dynamic_cast<ast::AsExpression*>(e))
        return evalExpr(a->expression.get(), env, thisV, strict);
    if (auto* nn = dynamic_cast<ast::NonNullExpression*>(e))
        return evalExpr(nn->expression.get(), env, thisV, strict);

    if (auto* t = dynamic_cast<ast::TemplateExpression*>(e)) {
        TsValue* acc = boxStr(t->head);
        for (auto& span : t->spans) {
            Cpl v = evalExpr(span.expression.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            TsValue* sv = nullptr; TsValue* ex = nullptr;
            if (!guardToString(v.v, &sv, &ex)) return thrown(ex);
            acc = ts_value_make_string(
                ts_string_concat(ts_value_get_string(acc), ts_value_get_string(sv)));
            if (!span.literal.empty())
                acc = ts_value_make_string(
                    ts_string_concat(ts_value_get_string(acc),
                                     ts_string_create(span.literal.c_str())));
        }
        return normal(acc);
    }

    if (auto* arr = dynamic_cast<ast::ArrayLiteralExpression*>(e)) {
        TsArray* a = TsArray::Create(arr->elements.size());
        for (auto& el : arr->elements) {
            if (dynamic_cast<ast::OmittedExpression*>(el.get()))
                return unsupported("array hole in eval code");
            if (dynamic_cast<ast::SpreadElement*>(el.get()))
                return unsupported("spread element");
            Cpl v = evalExpr(el.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            ts_array_push_any((void*)a, v.v);
        }
        return normal(boxObj(a));
    }

    if (auto* obj = dynamic_cast<ast::ObjectLiteralExpression*>(e)) {
        // Plain map with NULL [[Prototype]], exactly like AOT object
        // literals: gets fall back to Object.prototype dynamically, and
        // for-in enumerates own keys only.
        TsMap* m = (TsMap*)ts_map_create();
        TsValue* boxed = boxObj(m);
        for (auto& propNode : obj->properties) {
            if (auto* pa = dynamic_cast<ast::PropertyAssignment*>(propNode.get())) {
                TsValue* keyV;
                if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(pa->nameNode.get())) {
                    Cpl k = evalExpr(cpn->expression.get(), env, thisV, strict);
                    if (isAbrupt(k)) return k;
                    keyV = k.v;
                } else {
                    keyV = boxStr(pa->name);
                }
                Cpl v = evalExpr(pa->initializer.get(), env, thisV, strict);
                if (isAbrupt(v)) return v;
                TsValue* ex = nullptr;
                if (!guardSet(boxed, keyV, v.v, &ex)) return thrown(ex);
            } else if (auto* sp = dynamic_cast<ast::ShorthandPropertyAssignment*>(propNode.get())) {
                Cpl v = readIdent(env, sp->name, false);
                if (isAbrupt(v)) return v;
                TsValue* ex = nullptr;
                if (!guardSet(boxed, boxStr(sp->name), v.v, &ex)) return thrown(ex);
            } else if (auto* md = dynamic_cast<ast::MethodDefinition*>(propNode.get())) {
                if (md->isGetter || md->isSetter || md->isAsync || md->isGenerator)
                    return unsupported("object literal accessor/generator method");
                auto* d = new InterpFn();
                d->params = &md->parameters;
                d->body = &md->body;
                d->name = md->name;
                d->strict = strict || bodyHasUseStrict(md->body);
                fnRegistry().push_back(d);
                TsValue* fn = makeInterpClosure(d, env, thisV);
                TsValue* ex = nullptr;
                if (!guardSet(boxed, boxStr(md->name), fn, &ex)) return thrown(ex);
            } else {
                return unsupported("object literal member " + propNode->getKind());
            }
        }
        return normal(boxed);
    }

    if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(e)) {
        // new.target meta-property parses as PropertyAccess(Identifier"new",
        // "target"). Read the ambient [[NewTarget]] register.
        if (pa->name == "target")
            if (auto* nid = dynamic_cast<ast::Identifier*>(pa->expression.get()))
                if (nid->name == "new") {
                    TsValue* nt = (TsValue*)ts_get_new_target();
                    return normal(nt ? nt : jsUndefined());
                }
        // super.prop: read from [[HomeObject]].[[Prototype]] (receiver = this).
        if (dynamic_cast<ast::SuperExpression*>(pa->expression.get())) {
            TsValue* home = envLookupReserved(env, "\x01h");
            if (!home) return throwTyped("SyntaxError", "'super' keyword unexpected here");
            TsValue* sproto = ts_object_getPrototypeOf(home);
            if (!sproto || ts_value_is_nullish(sproto)) return normal(jsUndefined());
            TsValue* out = nullptr; TsValue* ex = nullptr;
            if (!guardGet(sproto, boxStr(pa->name), &out, &ex)) return thrown(ex);
            return normal(out ? out : jsUndefined());
        }
        Cpl o = evalExpr(pa->expression.get(), env, thisV, strict);
        if (isAbrupt(o)) return o;
        if (pa->isOptional && ts_value_is_nullish(o.v)) return normal(jsUndefined());
        if (ts_value_is_nullish(o.v))
            return throwTyped("TypeError",
                "Cannot read properties of " +
                std::string(ts_value_is_null(o.v) ? "null" : "undefined") +
                " (reading '" + pa->name + "')");
        TsValue* out = nullptr; TsValue* ex = nullptr;
        if (!guardGet(o.v, boxStr(pa->name), &out, &ex)) return thrown(ex);
        return normal(out ? out : jsUndefined());
    }

    if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(e)) {
        Cpl o = evalExpr(ea->expression.get(), env, thisV, strict);
        if (isAbrupt(o)) return o;
        if (ea->isOptional && ts_value_is_nullish(o.v)) return normal(jsUndefined());
        Cpl k = evalKey(ea->argumentExpression.get(), env, thisV, strict);
        if (isAbrupt(k)) return k;
        if (ts_value_is_nullish(o.v))
            return throwTyped("TypeError", "Cannot read properties of null or undefined");
        TsValue* out = nullptr; TsValue* ex = nullptr;
        if (!guardGet(o.v, k.v, &out, &ex)) return thrown(ex);
        return normal(out ? out : jsUndefined());
    }

    if (auto* ce = dynamic_cast<ast::CallExpression*>(e))
        return evalCall(ce, env, thisV, strict);

    if (auto* ne = dynamic_cast<ast::NewExpression*>(e)) {
        Cpl f = evalExpr(ne->expression.get(), env, thisV, strict);
        if (isAbrupt(f)) return f;
        TsArray* argsArr = TsArray::Create(ne->arguments.size());
        for (auto& a : ne->arguments) {
            if (dynamic_cast<ast::SpreadElement*>(a.get()))
                return unsupported("spread in new()");
            Cpl v = evalExpr(a.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            ts_array_push_any((void*)argsArr, v.v);
        }
        TsValue* out = nullptr; TsValue* ex = nullptr;
        if (!guardConstruct(f.v, boxObj(argsArr), &out, &ex)) return thrown(ex);
        return normal(out ? out : jsUndefined());
    }

    if (auto* be = dynamic_cast<ast::BinaryExpression*>(e))
        return evalBinary(be, env, thisV, strict);

    if (auto* cond = dynamic_cast<ast::ConditionalExpression*>(e)) {
        Cpl c = evalExpr(cond->condition.get(), env, thisV, strict);
        if (isAbrupt(c)) return c;
        return evalExpr(ts_value_to_bool(c.v) ? cond->whenTrue.get()
                                              : cond->whenFalse.get(),
                        env, thisV, strict);
    }

    if (auto* ae = dynamic_cast<ast::AssignmentExpression*>(e))
        return evalAssignment(ae, env, thisV, strict);

    if (auto* pre = dynamic_cast<ast::PrefixUnaryExpression*>(e)) {
        const std::string& op = pre->op;
        if (op == "typeof") {
            // typeof on a bare unresolvable identifier is "undefined".
            if (auto* id = dynamic_cast<ast::Identifier*>(pre->operand.get())) {
                Cpl v = readIdent(env, id->name, /*forTypeof*/true);
                if (isAbrupt(v)) return v;
                return normal(ts_value_make_string(ts_value_typeof(v.v)));
            }
            Cpl v = evalExpr(pre->operand.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            return normal(ts_value_make_string(ts_value_typeof(v.v)));
        }
        if (op == "void") {
            Cpl v = evalExpr(pre->operand.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            return normal(jsUndefined());
        }
        if (op == "++" || op == "--") {
            if (auto* id = dynamic_cast<ast::Identifier*>(pre->operand.get())) {
                Cpl v = readIdent(env, id->name, false);
                if (isAbrupt(v)) return v;
                Cpl nv = op1(op == "++" ? ts_value_inc : ts_value_dec, v.v);
                if (isAbrupt(nv)) return nv;
                Cpl a = assignIdent(env, id->name, nv.v, strict);
                if (isAbrupt(a)) return a;
                return normal(nv.v);
            }
            MemberRef ref;
            Cpl m = evalMemberRef(pre->operand.get(), env, thisV, strict, &ref);
            if (isAbrupt(m)) return m;
            TsValue* cur = nullptr; TsValue* ex = nullptr;
            if (!guardGet(ref.obj, ref.keyV, &cur, &ex)) return thrown(ex);
            Cpl nv = op1(op == "++" ? ts_value_inc : ts_value_dec, cur);
            if (isAbrupt(nv)) return nv;
            if (!guardSet(ref.obj, ref.keyV, nv.v, &ex)) return thrown(ex);
            return normal(nv.v);
        }
        Cpl v = evalExpr(pre->operand.get(), env, thisV, strict);
        if (isAbrupt(v)) return v;
        if (op == "!") return normal(jsBool(!ts_value_to_bool(v.v)));
        if (op == "-") return op1(ts_value_neg, v.v);
        if (op == "+") return op1(ts_value_pos, v.v);
        if (op == "~") return op1(ts_value_bitnot, v.v);
        return unsupported("unary operator " + op);
    }

    if (auto* post = dynamic_cast<ast::PostfixUnaryExpression*>(e)) {
        const std::string& op = post->op;
        if (auto* id = dynamic_cast<ast::Identifier*>(post->operand.get())) {
            Cpl v = readIdent(env, id->name, false);
            if (isAbrupt(v)) return v;
            // Postfix returns the OLD value coerced to number: x++ is
            // ToNumeric(oldValue). ts_value_pos performs ToNumber (throws for
            // BigInt mix exactly like the dispatcher).
            Cpl oldNum = op1(ts_value_pos, v.v);
            if (isAbrupt(oldNum)) return oldNum;
            Cpl nv = op1(op == "++" ? ts_value_inc : ts_value_dec, v.v);
            if (isAbrupt(nv)) return nv;
            Cpl a = assignIdent(env, id->name, nv.v, strict);
            if (isAbrupt(a)) return a;
            return normal(oldNum.v);
        }
        MemberRef ref;
        Cpl m = evalMemberRef(post->operand.get(), env, thisV, strict, &ref);
        if (isAbrupt(m)) return m;
        TsValue* cur = nullptr; TsValue* ex = nullptr;
        if (!guardGet(ref.obj, ref.keyV, &cur, &ex)) return thrown(ex);
        Cpl oldNum = op1(ts_value_pos, cur);
        if (isAbrupt(oldNum)) return oldNum;
        Cpl nv = op1(op == "++" ? ts_value_inc : ts_value_dec, cur);
        if (isAbrupt(nv)) return nv;
        if (!guardSet(ref.obj, ref.keyV, nv.v, &ex)) return thrown(ex);
        return normal(oldNum.v);
    }

    if (auto* del = dynamic_cast<ast::DeleteExpression*>(e)) {
        Expression* target = del->expression.get();
        if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(target))
            target = p->expression.get();
        MemberRef ref;
        if (dynamic_cast<ast::PropertyAccessExpression*>(target) ||
            dynamic_cast<ast::ElementAccessExpression*>(target)) {
            Cpl m = evalMemberRef(target, env, thisV, strict, &ref);
            if (isAbrupt(m)) return m;
            bool out = false; TsValue* ex = nullptr;
            if (!guardDelete(ref.obj, ref.keyV, &out, &ex)) return thrown(ex);
            return normal(jsBool(out));
        }
        if (auto* id = dynamic_cast<ast::Identifier*>(target)) {
            // delete on a bare name: only a global property can be deleted.
            bool out = false; TsValue* ex = nullptr;
            if (!guardDelete(globalThis, boxStr(id->name), &out, &ex)) return thrown(ex);
            return normal(jsBool(out));
        }
        Cpl v = evalExpr(target, env, thisV, strict); // evaluate for effect
        if (isAbrupt(v)) return v;
        return normal(jsBool(true));
    }

    if (auto* fe = dynamic_cast<ast::FunctionExpression*>(e)) {
        if (fe->isAsync || fe->isGenerator)
            return unsupported("async/generator function in eval code");
        InterpFn* d = fnDataForFunctionExpr(fe, strict);
        return normal(makeInterpClosure(d, env, thisV));
    }

    if (auto* ce = dynamic_cast<ast::ClassExpression*>(e))
        return evalClass(ce->name, ce->baseClass, ce->members, env, thisV, strict);

    if (auto* af = dynamic_cast<ast::ArrowFunction*>(e)) {
        if (af->isAsync) return unsupported("async arrow in eval code");
        auto* d = new InterpFn();
        d->params = &af->parameters;
        d->isArrow = true;
        d->strict = strict;
        if (auto* blk = dynamic_cast<ast::BlockStatement*>(af->body.get())) {
            d->body = &blk->statements;
            if (bodyHasUseStrict(blk->statements)) d->strict = true;
        } else {
            d->exprBody = dynamic_cast<Expression*>(af->body.get());
            if (!d->exprBody) { delete d; return unsupported("arrow body"); }
        }
        fnRegistry().push_back(d);
        return normal(makeInterpClosure(d, env, thisV));
    }

    return unsupported(e->getKind());
}

// Assign a computed value back to an lvalue expression (identifier/member).
Cpl assignTo(Expression* lhs, TsValue* v, TsMap* env, TsValue* thisV, bool strict) {
    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(lhs))
        lhs = p->expression.get();
    if (auto* id = dynamic_cast<ast::Identifier*>(lhs))
        return assignIdent(env, id->name, v, strict);
    MemberRef ref;
    Cpl m = evalMemberRef(lhs, env, thisV, strict, &ref);
    if (isAbrupt(m)) return m;
    TsValue* ex = nullptr;
    if (!guardSet(ref.obj, ref.keyV, v, &ex)) return thrown(ex);
    return normal(v);
}

// Sequence/comma, short-circuit, AND compound assignment (`+=` etc.) are all
// BinaryExpression ops in this AST (the parser desugars compound assignment
// to a BinaryExpression whose op keeps the '=' suffix).
Cpl evalBinary(ast::BinaryExpression* be, TsMap* env, TsValue* thisV, bool strict) {
    const std::string& op = be->op;

    // Logical assignment short-circuits BEFORE evaluating the RHS.
    if (op == "&&=" || op == "||=" || op == "??=") {
        Cpl l = evalExpr(be->left.get(), env, thisV, strict);
        if (isAbrupt(l)) return l;
        bool doAssign = (op == "&&=") ? ts_value_to_bool(l.v)
                      : (op == "||=") ? !ts_value_to_bool(l.v)
                                      : ts_value_is_nullish(l.v);
        if (!doAssign) return l;
        Cpl r = evalExpr(be->right.get(), env, thisV, strict);
        if (isAbrupt(r)) return r;
        Cpl a = assignTo(be->left.get(), r.v, env, thisV, strict);
        if (isAbrupt(a)) return a;
        return normal(r.v);
    }

    // Compound assignment: compute `left op right`, assign back, yield value.
    if (op.size() >= 2 && op.back() == '=' &&
        op != "==" && op != "!=" && op != "===" && op != "!==" &&
        op != "<=" && op != ">=") {
        std::string inner = op.substr(0, op.size() - 1);
        Op2 f = nullptr;
        if      (inner == "+")   f = ts_value_add;
        else if (inner == "-")   f = ts_value_sub;
        else if (inner == "*")   f = ts_value_mul;
        else if (inner == "/")   f = ts_value_div;
        else if (inner == "%")   f = ts_value_mod;
        else if (inner == "**")  f = ts_value_pow;
        else if (inner == "&")   f = ts_value_and;
        else if (inner == "|")   f = ts_value_or;
        else if (inner == "^")   f = ts_value_xor;
        else if (inner == "<<")  f = ts_value_shl;
        else if (inner == ">>")  f = ts_value_sar;
        else if (inner == ">>>") f = ts_value_ushr;
        if (f) {
            Cpl l = evalExpr(be->left.get(), env, thisV, strict);
            if (isAbrupt(l)) return l;
            Cpl r = evalExpr(be->right.get(), env, thisV, strict);
            if (isAbrupt(r)) return r;
            Cpl v = op2(f, l.v, r.v);
            if (isAbrupt(v)) return v;
            Cpl a = assignTo(be->left.get(), v.v, env, thisV, strict);
            if (isAbrupt(a)) return a;
            return normal(v.v);
        }
        return unsupported("compound assignment " + op);
    }

    if (op == "&&" || op == "||" || op == "??") {
        Cpl l = evalExpr(be->left.get(), env, thisV, strict);
        if (isAbrupt(l)) return l;
        if (op == "&&") {
            if (!ts_value_to_bool(l.v)) return l;
        } else if (op == "||") {
            if (ts_value_to_bool(l.v)) return l;
        } else {
            if (!ts_value_is_nullish(l.v)) return l;
        }
        return evalExpr(be->right.get(), env, thisV, strict);
    }
    if (op == ",") {
        Cpl l = evalExpr(be->left.get(), env, thisV, strict);
        if (isAbrupt(l)) return l;
        return evalExpr(be->right.get(), env, thisV, strict);
    }

    Cpl l = evalExpr(be->left.get(), env, thisV, strict);
    if (isAbrupt(l)) return l;
    Cpl r = evalExpr(be->right.get(), env, thisV, strict);
    if (isAbrupt(r)) return r;

    if (op == "+")   return op2(ts_value_add, l.v, r.v);
    if (op == "-")   return op2(ts_value_sub, l.v, r.v);
    if (op == "*")   return op2(ts_value_mul, l.v, r.v);
    if (op == "/")   return op2(ts_value_div, l.v, r.v);
    if (op == "%")   return op2(ts_value_mod, l.v, r.v);
    if (op == "**")  return op2(ts_value_pow, l.v, r.v);
    if (op == "&")   return op2(ts_value_and, l.v, r.v);
    if (op == "|")   return op2(ts_value_or, l.v, r.v);
    if (op == "^")   return op2(ts_value_xor, l.v, r.v);
    if (op == "<<")  return op2(ts_value_shl, l.v, r.v);
    if (op == ">>")  return op2(ts_value_sar, l.v, r.v);
    if (op == ">>>") return op2(ts_value_ushr, l.v, r.v);
    if (op == "<")   return op2(ts_value_lt, l.v, r.v);
    if (op == "<=")  return op2(ts_value_lte, l.v, r.v);
    if (op == ">")   return op2(ts_value_gt, l.v, r.v);
    if (op == ">=")  return op2(ts_value_gte, l.v, r.v);
    if (op == "==")  return op2(ts_value_eq, l.v, r.v);
    if (op == "!=") {
        Cpl c = op2(ts_value_eq, l.v, r.v);
        if (isAbrupt(c)) return c;
        return normal(jsBool(!ts_value_to_bool(c.v)));
    }
    if (op == "===") return normal(jsBool(ts_value_strict_eq_bool(l.v, r.v)));
    if (op == "!==") return normal(jsBool(!ts_value_strict_eq_bool(l.v, r.v)));
    if (op == "instanceof") {
        bool out = false; TsValue* ex = nullptr;
        if (!guardInstanceof(l.v, r.v, &out, &ex)) return thrown(ex);
        return normal(jsBool(out));
    }
    if (op == "in") {
        if (ts_value_is_nullish(r.v) || !ts_value_get_object(r.v))
            return throwTyped("TypeError",
                              "Cannot use 'in' operator to search in non-object");
        bool out = false; TsValue* ex = nullptr;
        if (!guardHas(r.v, l.v, &out, &ex)) return thrown(ex);
        return normal(jsBool(out));
    }
    return unsupported("binary operator " + op);
}

Cpl evalAssignment(ast::AssignmentExpression* ae, TsMap* env, TsValue* thisV,
                   bool strict) {
    // The parser encodes compound assignment as op on the AssignmentExpression?
    // In this AST, AssignmentExpression is plain '='; compound forms arrive as
    // BinaryExpression with ops like "+=" — handle both defensively.
    Cpl r = evalExpr(ae->right.get(), env, thisV, strict);
    if (isAbrupt(r)) return r;

    Expression* lhs = ae->left.get();
    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(lhs))
        lhs = p->expression.get();

    if (auto* id = dynamic_cast<ast::Identifier*>(lhs))
        return assignIdent(env, id->name, r.v, strict);

    if (dynamic_cast<ast::PropertyAccessExpression*>(lhs) ||
        dynamic_cast<ast::ElementAccessExpression*>(lhs)) {
        MemberRef ref;
        Cpl m = evalMemberRef(lhs, env, thisV, strict, &ref);
        if (isAbrupt(m)) return m;
        if (ts_value_is_nullish(ref.obj))
            return throwTyped("TypeError", "Cannot set properties of null or undefined");
        TsValue* ex = nullptr;
        if (!guardSet(ref.obj, ref.keyV, r.v, &ex)) return thrown(ex);
        return normal(r.v);
    }
    return unsupported("assignment target " + lhs->getKind());
}

// --- classes ----------------------------------------------------------------
// Initialize a class's instance fields on `thisV`. Runs at construction time
// (base ctor entry, or after super() for a derived ctor). Field initializers
// evaluate in the class scope with `this` bound to the new instance.
Cpl initInstanceFields(InterpFn* fd, TsMap* env, TsValue* thisV, bool strict) {
    if (!fd->fields) return normal();
    for (auto* pd : *fd->fields) {
        TsValue* keyV = boxStr(pd->name);
        if (pd->nameNode)
            if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(pd->nameNode.get())) {
                Cpl k = evalExpr(cpn->expression.get(), env, thisV, strict);
                if (isAbrupt(k)) return k;
                keyV = k.v;
            }
        TsValue* val = jsUndefined();
        if (pd->initializer) {
            Cpl v = evalExpr(pd->initializer.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            val = v.v;
        }
        TsValue* ex = nullptr;
        if (!guardSet(thisV, keyV, val, &ex)) return thrown(ex);
    }
    return normal();
}

// Build a class: constructor closure + prototype carrying methods, static
// members on the constructor, and extends/super linkage. Class bodies are
// always strict. Returns the boxed constructor. Async/generator methods,
// #private members, static blocks, and decorators are not supported here
// (documented eval N/A) and skipped.
Cpl evalClass(const std::string& className, const std::string& baseClassName,
              std::vector<ast::NodePtr>& members, TsMap* env, TsValue* thisV,
              bool strict) {
    TsValue* baseCtor = nullptr;
    TsValue* baseProto = nullptr;
    bool derived = !baseClassName.empty();
    if (derived) {
        Cpl b = readIdent(env, baseClassName, false);
        if (isAbrupt(b)) return b;
        baseCtor = b.v;
        if (!baseCtor || !ts_is_callable(baseCtor))
            return throwTyped("TypeError", "Class extends value is not a constructor");
        TsValue* ex = nullptr;
        if (!guardGet(baseCtor, boxStr("prototype"), &baseProto, &ex)) return thrown(ex);
    }

    // Class scope: name binds to the constructor (const) so members can
    // reference it; ctor + methods capture this env.
    TsMap* cenv = envNew(env, false);

    ast::MethodDefinition* ctorMD = nullptr;
    std::vector<ast::MethodDefinition*> methods, statics;
    auto* fields = new std::vector<ast::PropertyDefinition*>();
    std::vector<ast::PropertyDefinition*> staticFields;
    for (auto& mp : members) {
        if (auto* md = dynamic_cast<ast::MethodDefinition*>(mp.get())) {
            if (md->isAsync || md->isGenerator) continue;   // eval N/A
            if (!md->isStatic && md->name == "constructor" && !md->isGetter && !md->isSetter) {
                ctorMD = md; continue;
            }
            (md->isStatic ? statics : methods).push_back(md);
        } else if (auto* pd = dynamic_cast<ast::PropertyDefinition*>(mp.get())) {
            (pd->isStatic ? staticFields : *fields).push_back(pd);
        }
        // StaticBlock / IndexSignature: skipped
    }

    auto* cdata = new InterpFn();
    cdata->name = className;
    cdata->strict = true;
    cdata->isCtor = true;
    cdata->isDerivedCtor = derived;
    cdata->fields = fields;
    if (ctorMD) { cdata->params = &ctorMD->parameters; cdata->body = &ctorMD->body; }
    else cdata->isImplicitCtor = true;
    if (derived) cdata->superCtor = baseCtor;
    fnRegistry().push_back(cdata);
    TsValue* ctor = makeInterpClosure(cdata, cenv, thisV);

    TsValue* proto = ts_value_make_object(ts_object_create_empty());
    { TsValue* ex = nullptr;
      if (!guardSet(ctor, boxStr("prototype"), proto, &ex)) return thrown(ex); }
    ts_object_set_method(proto, boxStr("constructor"), ctor);
    cdata->homeObject = proto;

    auto installMethod = [&](ast::MethodDefinition* md, TsValue* home) -> Cpl {
        auto* d = new InterpFn();
        d->params = &md->parameters;
        d->body = &md->body;
        d->name = md->name;
        d->strict = true;
        d->homeObject = home;
        if (derived) d->superCtor = baseCtor;
        fnRegistry().push_back(d);
        TsValue* fn = makeInterpClosure(d, cenv, thisV);
        TsValue* keyV = boxStr(md->name);
        bool computed = false;
        if (md->nameNode)
            if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(md->nameNode.get())) {
                Cpl k = evalExpr(cpn->expression.get(), cenv, thisV, strict);
                if (isAbrupt(k)) return k;
                keyV = k.v; computed = true;
            }
        if (md->isGetter) {
            if (computed) ts_class_install_computed_getter(home, keyV, fn);
            else ts_object_set_method(home, boxStr("__getter_" + md->name), fn);
        } else if (md->isSetter) {
            if (computed) ts_class_install_computed_setter(home, keyV, fn);
            else ts_object_set_method(home, boxStr("__setter_" + md->name), fn);
        } else {
            ts_object_set_method(home, keyV, fn);
        }
        return normal();
    };
    for (auto* md : methods) { Cpl r = installMethod(md, proto); if (isAbrupt(r)) return r; }
    for (auto* md : statics) { Cpl r = installMethod(md, ctor);  if (isAbrupt(r)) return r; }

    if (derived) {
        ts_object_setPrototypeOf(proto, baseProto ? baseProto : jsNull());
        ts_object_setPrototypeOf(ctor, baseCtor);
    }

    if (!className.empty()) envDefine(cenv, className, ctor, true);

    for (auto* pd : staticFields) {
        TsValue* keyV = boxStr(pd->name);
        if (pd->nameNode)
            if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(pd->nameNode.get())) {
                Cpl k = evalExpr(cpn->expression.get(), cenv, ctor, strict);
                if (isAbrupt(k)) return k;
                keyV = k.v;
            }
        TsValue* val = jsUndefined();
        if (pd->initializer) {
            Cpl v = evalExpr(pd->initializer.get(), cenv, ctor, strict);
            if (isAbrupt(v)) return v;
            val = v.v;
        }
        TsValue* ex = nullptr;
        if (!guardSet(ctor, keyV, val, &ex)) return thrown(ex);
    }

    return normal(ctor);
}

Cpl evalCall(ast::CallExpression* ce, TsMap* env, TsValue* thisV, bool strict) {
    Expression* callee = ce->callee.get();
    if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(callee))
        callee = p->expression.get();

    // super(...): call the base constructor with the current `this`, then run
    // this (derived) class's instance-field initializers.
    if (dynamic_cast<ast::SuperExpression*>(callee)) {
        TsValue* sc = envLookupReserved(env, "\x01sc");
        if (!sc) return throwTyped("SyntaxError", "'super' keyword unexpected here");
        std::vector<TsValue*> argv;
        for (auto& a : ce->arguments) {
            if (dynamic_cast<ast::SpreadElement*>(a.get()))
                return unsupported("spread in super()");
            Cpl v = evalExpr(a.get(), env, thisV, strict);
            if (isAbrupt(v)) return v;
            argv.push_back(v.v);
        }
        TsValue* out = nullptr; TsValue* ex = nullptr;
        if (!guardCall(sc, thisV, (int)argv.size(),
                       argv.empty() ? nullptr : argv.data(), &out, &ex))
            return thrown(ex);
        // Instance fields initialize immediately after super() returns.
        if (TsValue* cf = envLookupReserved(env, "\x01" "cf")) {
            InterpFn* cfd = (InterpFn*)(intptr_t)ts_value_get_int(cf);
            if (cfd) { Cpl fr = initInstanceFields(cfd, env, thisV, strict);
                       if (isAbrupt(fr)) return fr; }
        }
        return normal(jsUndefined());
    }

    // super.method(...) / super[expr](...): resolve on [[HomeObject]] proto,
    // invoke with `this` = current this (not super).
    if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(callee)) {
        if (dynamic_cast<ast::SuperExpression*>(pa->expression.get())) {
            TsValue* home = envLookupReserved(env, "\x01h");
            if (!home) return throwTyped("SyntaxError", "'super' keyword unexpected here");
            TsValue* sproto = ts_object_getPrototypeOf(home);
            TsValue* fnV = nullptr; TsValue* ex = nullptr;
            if (sproto && !ts_value_is_nullish(sproto) &&
                !guardGet(sproto, boxStr(pa->name), &fnV, &ex)) return thrown(ex);
            if (!fnV || !ts_is_callable(fnV))
                return throwTyped("TypeError", "(intermediate value)." + pa->name + " is not a function");
            std::vector<TsValue*> argv;
            for (auto& a : ce->arguments) {
                if (dynamic_cast<ast::SpreadElement*>(a.get()))
                    return unsupported("spread argument");
                Cpl v = evalExpr(a.get(), env, thisV, strict);
                if (isAbrupt(v)) return v;
                argv.push_back(v.v);
            }
            TsValue* out = nullptr;
            if (!guardCall(fnV, thisV, (int)argv.size(),
                           argv.empty() ? nullptr : argv.data(), &out, &ex))
                return thrown(ex);
            return normal(out ? out : jsUndefined());
        }
    }

    // Direct eval inside eval'd code: run in the CURRENT interpreter scope —
    // the one place ts-aot can honor direct-eval semantics, because
    // interpreted frames have real environment records.
    if (auto* id = dynamic_cast<ast::Identifier*>(callee)) {
        if (id->name == "eval" && !ce->arguments.empty()) {
            Cpl a0 = evalExpr(ce->arguments[0].get(), env, thisV, strict);
            if (isAbrupt(a0)) return a0;
            void* sraw = ts_value_get_string(a0.v);
            if (!sraw) return normal(a0.v); // non-string argument returns as-is
            const char* src = ((TsString*)sraw)->ToUtf8();
            void* h = ts_parse_program(src ? src : "", "<eval>", 0);
            const char* perr = ts_parse_error(h);
            if (perr) {
                std::string msg(perr);
                ts_parse_free(h);
                return throwTyped("SyntaxError", msg);
            }
            retainParseHandle(h);
            auto* prog = (ast::Program*)ts_parse_get_program(h);
            return runProgramInEnv(prog, env, thisV, strict);
        }
    }

    TsValue* fnV = nullptr;
    TsValue* thisArg = jsUndefined();

    if (auto* pa = dynamic_cast<ast::PropertyAccessExpression*>(callee)) {
        Cpl o = evalExpr(pa->expression.get(), env, thisV, strict);
        if (isAbrupt(o)) return o;
        if ((pa->isOptional || ce->isOptional) && ts_value_is_nullish(o.v))
            return normal(jsUndefined());
        if (ts_value_is_nullish(o.v))
            return throwTyped("TypeError",
                "Cannot read properties of null or undefined (reading '" + pa->name + "')");
        TsValue* ex = nullptr;
        if (!guardGet(o.v, boxStr(pa->name), &fnV, &ex)) return thrown(ex);
        thisArg = o.v;
        if (ce->isOptional && (!fnV || ts_value_is_nullish(fnV)))
            return normal(jsUndefined());
        if (!fnV || ts_value_is_nullish(fnV) || !ts_is_callable(fnV))
            return throwTyped("TypeError", pa->name + " is not a function");
    } else if (auto* ea = dynamic_cast<ast::ElementAccessExpression*>(callee)) {
        Cpl o = evalExpr(ea->expression.get(), env, thisV, strict);
        if (isAbrupt(o)) return o;
        if ((ea->isOptional || ce->isOptional) && ts_value_is_nullish(o.v))
            return normal(jsUndefined());
        Cpl k = evalKey(ea->argumentExpression.get(), env, thisV, strict);
        if (isAbrupt(k)) return k;
        if (ts_value_is_nullish(o.v))
            return throwTyped("TypeError", "Cannot read properties of null or undefined");
        TsValue* ex = nullptr;
        if (!guardGet(o.v, k.v, &fnV, &ex)) return thrown(ex);
        thisArg = o.v;
        if (!fnV || ts_value_is_nullish(fnV) || !ts_is_callable(fnV))
            return throwTyped("TypeError", "value is not a function");
    } else {
        Cpl f = evalExpr(callee, env, thisV, strict);
        if (isAbrupt(f)) return f;
        fnV = f.v;
        if (ce->isOptional && ts_value_is_nullish(fnV)) return normal(jsUndefined());
        if (!fnV || ts_value_is_nullish(fnV) || !ts_is_callable(fnV)) {
            std::string what = "value";
            if (auto* id = dynamic_cast<ast::Identifier*>(callee)) what = id->name;
            return throwTyped("TypeError", what + " is not a function");
        }
    }

    std::vector<TsValue*> argv;
    argv.reserve(ce->arguments.size());
    for (auto& a : ce->arguments) {
        if (dynamic_cast<ast::SpreadElement*>(a.get()))
            return unsupported("spread argument");
        Cpl v = evalExpr(a.get(), env, thisV, strict);
        if (isAbrupt(v)) return v;
        argv.push_back(v.v);
    }

    TsValue* out = nullptr; TsValue* ex = nullptr;
    if (!guardCall(fnV, thisArg, (int)argv.size(),
                   argv.empty() ? nullptr : argv.data(), &out, &ex))
        return thrown(ex);
    return normal(out ? out : jsUndefined());
}

// --- statements ---------------------------------------------------------------

// --- destructuring binding patterns -----------------------------------------
// One recursive binder shared by declarators, parameters, for-of/in heads and
// catch clauses. `mode` (declared up top) decides how a leaf is created.
Cpl bindLeaf(const std::string& name, TsValue* v, TsMap* env, BindMode mode) {
    if (mode == BindMode::Var) {
        TsMap* target = envVarTarget(env);
        if (envIsGlobalVarScope(target)) {
            TsValue* ex = nullptr;
            if (!guardSet(globalThis, boxStr(name), v, &ex)) return thrown(ex);
        } else {
            envDefine(target, name, v, false);
        }
    } else {
        envDefine(env, name, v, mode == BindMode::Const);
    }
    return normal();
}

// Property key of an object binding element (computed / explicit / shorthand).
Cpl bindElemKey(ast::BindingElement* be, TsMap* env, TsValue* thisV, bool strict,
                TsValue** outKey) {
    if (be->computedPropertyName) {
        if (auto* cpn = dynamic_cast<ast::ComputedPropertyName*>(be->computedPropertyName.get())) {
            Cpl k = evalExpr(cpn->expression.get(), env, thisV, strict);
            if (isAbrupt(k)) return k;
            *outKey = k.v; return normal();
        }
    }
    if (!be->propertyName.empty()) { *outKey = boxStr(be->propertyName); return normal(); }
    if (auto* id = dynamic_cast<ast::Identifier*>(be->name.get())) {
        *outKey = boxStr(id->name); return normal();
    }
    *outKey = boxStr(""); return normal();
}

// Pull one value from an array-pattern iterator; sets *done when exhausted.
bool patNext(TsValue* iter, bool* done, TsValue** valOut, TsValue** ex) {
    *valOut = jsUndefined();
    if (*done) return true;
    TsValue* res = nullptr;
    if (!guardIterNext(iter, &res, ex)) return false;
    if (!res) { *done = true; return true; }
    TsValue* dn = nullptr;
    if (!guardGet(res, boxStr("done"), &dn, ex)) return false;
    if (dn && ts_value_to_bool(dn)) { *done = true; return true; }
    TsValue* vv = nullptr;
    if (!guardGet(res, boxStr("value"), &vv, ex)) return false;
    *valOut = vv ? vv : jsUndefined();
    return true;
}

Cpl bindPattern(ast::Node* target, TsValue* v, TsMap* env, TsValue* thisV,
                bool strict, BindMode mode) {
    if (!v) v = jsUndefined();
    if (auto* id = dynamic_cast<ast::Identifier*>(target))
        return bindLeaf(id->name, v, env, mode);

    if (auto* obp = dynamic_cast<ast::ObjectBindingPattern*>(target)) {
        if (ts_value_is_nullish(v))
            return throwTyped("TypeError",
                "Cannot destructure a nullish value.");
        std::set<std::string> seen;
        for (auto& elp : obp->elements) {
            auto* be = dynamic_cast<ast::BindingElement*>(elp.get());
            if (!be) continue;
            if (be->isSpread) {
                // Rest: own enumerable string keys of v not already consumed.
                TsValue* boxedRest = boxObj((TsMap*)ts_map_create());
                TsValue* keysV = ts_object_keys(v);
                void* keysRaw = ts_value_get_object(keysV);
                int64_t n = keysRaw ? ts_array_length(keysRaw) : 0;
                for (int64_t i = 0; i < n; i++) {
                    TsValue* kk = ts_array_get_dynamic(keysV, ts_value_make_int(i));
                    TsString* ks = (TsString*)ts_value_get_string(kk);
                    if (ks && seen.count(ks->ToUtf8())) continue;
                    TsValue* pv = nullptr; TsValue* ex = nullptr;
                    if (!guardGet(v, kk, &pv, &ex)) return thrown(ex);
                    if (!guardSet(boxedRest, kk, pv ? pv : jsUndefined(), &ex)) return thrown(ex);
                }
                Cpl r = bindPattern(be->name.get(), boxedRest, env, thisV, strict, mode);
                if (isAbrupt(r)) return r;
                continue;
            }
            TsValue* keyV = nullptr;
            Cpl kc = bindElemKey(be, env, thisV, strict, &keyV);
            if (isAbrupt(kc)) return kc;
            if (TsString* ks = (TsString*)ts_value_get_string(keyV)) seen.insert(ks->ToUtf8());
            TsValue* pv = nullptr; TsValue* ex = nullptr;
            if (!guardGet(v, keyV, &pv, &ex)) return thrown(ex);
            if ((!pv || ts_value_is_undefined(pv)) && be->initializer) {
                Cpl d = evalExpr(be->initializer.get(), env, thisV, strict);
                if (isAbrupt(d)) return d;
                pv = d.v;
            }
            Cpl r = bindPattern(be->name.get(), pv, env, thisV, strict, mode);
            if (isAbrupt(r)) return r;
        }
        return normal();
    }

    if (auto* abp = dynamic_cast<ast::ArrayBindingPattern*>(target)) {
        if (ts_value_is_nullish(v))
            return throwTyped("TypeError", "value is not iterable");
        TsValue* iter = nullptr; TsValue* ex = nullptr;
        if (!guardIterGet(v, &iter, &ex)) return thrown(ex);
        bool done = false;
        for (auto& elp : abp->elements) {
            auto* be = dynamic_cast<ast::BindingElement*>(elp.get());
            if (!be) {                      // elision hole: consume one, skip
                TsValue* skip = nullptr;
                if (!patNext(iter, &done, &skip, &ex)) return thrown(ex);
                continue;
            }
            if (be->isSpread) {
                TsArray* arr = TsArray::Create(0);
                TsValue* boxedArr = boxObj(arr);
                TsValue* val = nullptr;
                while (!done) {
                    if (!patNext(iter, &done, &val, &ex)) return thrown(ex);
                    if (done) break;
                    ts_array_push_any((void*)arr, val);
                }
                Cpl r = bindPattern(be->name.get(), boxedArr, env, thisV, strict, mode);
                if (isAbrupt(r)) return r;
                continue;
            }
            TsValue* val = nullptr;
            if (!patNext(iter, &done, &val, &ex)) return thrown(ex);
            if (ts_value_is_undefined(val) && be->initializer) {
                Cpl d = evalExpr(be->initializer.get(), env, thisV, strict);
                if (isAbrupt(d)) return d;
                val = d.v;
            }
            Cpl r = bindPattern(be->name.get(), val, env, thisV, strict, mode);
            if (isAbrupt(r)) return r;
        }
        if (!done) iterCloseQuiet(iter);
        return normal();
    }

    return unsupported("destructuring target " +
                       std::string(target ? target->getKind() : "null"));
}

Cpl execVarDecl(ast::VariableDeclaration* vd, TsMap* env, TsValue* thisV, bool strict) {
    auto* id = dynamic_cast<ast::Identifier*>(vd->name.get());

    TsValue* v = jsUndefined();
    if (vd->initializer) {
        Cpl c = evalExpr(vd->initializer.get(), env, thisV, strict);
        if (isAbrupt(c)) return c;
        v = c.v;
    }

    if (!id) {
        BindMode m = vd->varKind == ast::VarKind::Var ? BindMode::Var
                   : vd->varKind == ast::VarKind::Const ? BindMode::Const
                   : BindMode::Let;
        return bindPattern(vd->name.get(), v, env, thisV, strict, m);
    }

    if (vd->varKind == ast::VarKind::Var) {
        TsMap* target = envVarTarget(env);
        if (envIsGlobalVarScope(target)) {
            // Hoisting already created the slot; only overwrite when an
            // initializer ran (`var x;` must not clobber an existing global).
            if (vd->initializer) {
                TsValue* ex = nullptr;
                if (!guardSet(globalThis, boxStr(id->name), v, &ex)) return thrown(ex);
            }
        } else {
            if (vd->initializer || !envHasOwn(target, id->name))
                envDefine(target, id->name, v, false);
        }
        return normal();
    }
    // let/const: initialize the (pre-declared TDZ) binding in the CURRENT env.
    envDefine(env, id->name, v, vd->varKind == ast::VarKind::Const);
    return normal();
}

Cpl execBlock(ast::BlockStatement* b, TsMap* env, TsValue* thisV, bool strict) {
    if (b->withHead) {
        if (strict) return throwTyped("SyntaxError",
                                      "Strict mode code may not include a with statement");
        Cpl h = evalExpr(b->withHead.get(), env, thisV, strict);
        if (isAbrupt(h)) return h;
        if (ts_value_is_nullish(h.v))
            return throwTyped("TypeError", "Cannot convert undefined or null to object");
        TsMap* wenv = envNew(env, false);
        wenv->Set(key("\x01w"), nanbox_to_tagged(h.v));
        predeclareLexical(b->statements, wenv);
        instantiateBlockFns(b->statements, wenv, thisV, strict);
        return execStmts(b->statements, wenv, thisV, strict);
    }
    TsMap* benv = b->isSynthetic ? env : envNew(env, false);
    if (!b->isSynthetic) {
        predeclareLexical(b->statements, benv);
        instantiateBlockFns(b->statements, benv, thisV, strict);
    }
    return execStmts(b->statements, benv, thisV, strict);
}

Cpl execStmts(std::vector<ast::StmtPtr>& stmts, TsMap* env, TsValue* thisV,
              bool strict) {
    TsValue* completion = nullptr;
    for (auto& s : stmts) {
        Cpl c = execStmt(s.get(), env, thisV, strict);
        if (isAbrupt(c)) return c;
        if (c.v) completion = c.v;   // track statement completion values
    }
    return normal(completion);
}

// Loop-body completion handling: returns true when the loop should CONTINUE
// after this body completion, false when the caller must return `out`.
bool loopBody(const Cpl& c, const std::string& label, Cpl* out, TsValue** completion) {
    if (c.k == Cpl::Normal) { if (c.v) *completion = c.v; return true; }
    if (c.k == Cpl::Cont && (c.label.empty() || c.label == label)) return true;
    if (c.k == Cpl::Brk && (c.label.empty() || c.label == label)) {
        *out = normal(*completion);
        return false;
    }
    *out = c;
    return false;
}

Cpl execLoopWhile(ast::WhileStatement* w, TsMap* env, TsValue* thisV,
                  bool strict, const std::string& label) {
    TsValue* completion = nullptr;
    if (w->isDoWhile) {
        for (;;) {
            Cpl b = execStmt(w->body.get(), env, thisV, strict);
            Cpl out;
            if (!loopBody(b, label, &out, &completion)) return out;
            Cpl c = evalExpr(w->condition.get(), env, thisV, strict);
            if (isAbrupt(c)) return c;
            if (!ts_value_to_bool(c.v)) break;
        }
        return normal(completion);
    }
    for (;;) {
        Cpl c = evalExpr(w->condition.get(), env, thisV, strict);
        if (isAbrupt(c)) return c;
        if (!ts_value_to_bool(c.v)) break;
        Cpl b = execStmt(w->body.get(), env, thisV, strict);
        Cpl out;
        if (!loopBody(b, label, &out, &completion)) return out;
    }
    return normal(completion);
}

Cpl execLoopFor(ast::ForStatement* f, TsMap* env, TsValue* thisV,
                bool strict, const std::string& label) {
    TsMap* fenv = envNew(env, false);
    if (f->initializer) {
        if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(f->initializer.get())) {
            if (vd->varKind != ast::VarKind::Var) {
                if (auto* id = dynamic_cast<ast::Identifier*>(vd->name.get()))
                    fenv->Set(key(id->name), nanbox_to_tagged(boxObj(tdzMarker())));
            }
        }
        Cpl i = execStmt(f->initializer.get(), fenv, thisV, strict);
        if (isAbrupt(i)) return i;
    }
    TsValue* completion = nullptr;
    for (;;) {
        if (f->condition) {
            Cpl c = evalExpr(f->condition.get(), fenv, thisV, strict);
            if (isAbrupt(c)) return c;
            if (!ts_value_to_bool(c.v)) break;
        }
        Cpl b = execStmt(f->body.get(), fenv, thisV, strict);
        Cpl out;
        if (!loopBody(b, label, &out, &completion)) return out;
        if (f->incrementor) {
            Cpl inc = evalExpr(f->incrementor.get(), fenv, thisV, strict);
            if (isAbrupt(inc)) return inc;
        }
    }
    return normal(completion);
}

// Bind the for-of / for-in loop variable for one iteration. The initializer
// is either a VariableDeclaration (fresh binding per iteration for let/const,
// var-scope write for var) or a bare identifier assignment target.
Cpl bindLoopVar(ast::Statement* init, TsValue* v, TsMap* ienv, TsValue* thisV,
                bool strict) {
    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(init)) {
        auto* id = dynamic_cast<ast::Identifier*>(vd->name.get());
        if (!id) {
            BindMode m = vd->varKind == ast::VarKind::Var ? BindMode::Var
                       : vd->varKind == ast::VarKind::Const ? BindMode::Const
                       : BindMode::Let;
            return bindPattern(vd->name.get(), v, ienv, thisV, strict, m);
        }
        if (vd->varKind == ast::VarKind::Var) {
            TsMap* target = envVarTarget(ienv);
            if (envIsGlobalVarScope(target)) {
                TsValue* ex = nullptr;
                if (!guardSet(globalThis, boxStr(id->name), v, &ex)) return thrown(ex);
            } else {
                envDefine(target, id->name, v, false);
            }
        } else {
            envDefine(ienv, id->name, v, vd->varKind == ast::VarKind::Const);
        }
        return normal();
    }
    if (auto* es = dynamic_cast<ast::ExpressionStatement*>(init)) {
        if (auto* id = dynamic_cast<ast::Identifier*>(es->expression.get()))
            return assignIdent(ienv, id->name, v, strict);
        return assignTo(es->expression.get(), v, ienv, thisV, strict);
    }
    return unsupported("loop binding " + std::string(init ? init->getKind() : "null"));
}

Cpl execForOf(ast::ForOfStatement* fo, TsMap* env, TsValue* thisV, bool strict,
              const std::string& label) {
    if (fo->isAwait) return unsupported("for await...of in eval code");
    Cpl it = evalExpr(fo->expression.get(), env, thisV, strict);
    if (isAbrupt(it)) return it;
    if (ts_value_is_nullish(it.v))
        return throwTyped("TypeError", "undefined is not iterable");
    TsValue* iter = nullptr; TsValue* ex = nullptr;
    if (!guardIterGet(it.v, &iter, &ex)) return thrown(ex);

    TsValue* completion = nullptr;
    for (;;) {
        TsValue* res = nullptr;
        if (!guardIterNext(iter, &res, &ex)) return thrown(ex);
        if (!res) break;
        TsValue* done = nullptr;
        if (!guardGet(res, boxStr("done"), &done, &ex)) return thrown(ex);
        if (done && ts_value_to_bool(done)) break;
        TsValue* val = nullptr;
        if (!guardGet(res, boxStr("value"), &val, &ex)) return thrown(ex);

        TsMap* ienv = envNew(env, false);
        Cpl bind = bindLoopVar(fo->initializer.get(), val ? val : jsUndefined(),
                               ienv, thisV, strict);
        if (isAbrupt(bind)) { iterCloseQuiet(iter); return bind; }
        Cpl b = execStmt(fo->body.get(), ienv, thisV, strict);
        Cpl out;
        if (!loopBody(b, label, &out, &completion)) {
            iterCloseQuiet(iter);   // ES 7.4.8 IteratorClose on abrupt exit
            return out;
        }
    }
    return normal(completion);
}

Cpl execForIn(ast::ForInStatement* fi, TsMap* env, TsValue* thisV, bool strict,
              const std::string& label) {
    Cpl obj = evalExpr(fi->expression.get(), env, thisV, strict);
    if (isAbrupt(obj)) return obj;
    // for-in over null/undefined iterates zero times (ES 14.7.5.6).
    if (ts_value_is_nullish(obj.v)) return normal();
    TsValue* keysV = nullptr; TsValue* ex = nullptr;
    if (!guardForInKeys(obj.v, &keysV, &ex)) return thrown(ex);
    void* keysRaw = ts_value_get_object(keysV);
    if (!keysRaw) return normal();
    int64_t n = ts_array_length(keysRaw);

    TsValue* completion = nullptr;
    for (int64_t i = 0; i < n; i++) {
        TsValue* k = ts_array_get_dynamic(keysV, ts_value_make_int(i));
        TsMap* ienv = envNew(env, false);
        Cpl bind = bindLoopVar(fi->initializer.get(), k ? k : jsUndefined(),
                               ienv, thisV, strict);
        if (isAbrupt(bind)) return bind;
        Cpl b = execStmt(fi->body.get(), ienv, thisV, strict);
        Cpl out;
        if (!loopBody(b, label, &out, &completion)) return out;
    }
    return normal(completion);
}

Cpl execSwitch(ast::SwitchStatement* sw, TsMap* env, TsValue* thisV, bool strict) {
    Cpl d = evalExpr(sw->expression.get(), env, thisV, strict);
    if (isAbrupt(d)) return d;
    TsMap* senv = envNew(env, false);

    // The CaseBlock is one lexical scope: TDZ its let/const and instantiate
    // block-level fns (all clauses) before evaluating case expressions.
    for (auto& cl : sw->clauses) {
        std::vector<ast::StmtPtr>* cs = nullptr;
        if (auto* cc = dynamic_cast<ast::CaseClause*>(cl.get())) cs = &cc->statements;
        else if (auto* dc = dynamic_cast<ast::DefaultClause*>(cl.get())) cs = &dc->statements;
        if (!cs) continue;
        predeclareLexical(*cs, senv);
        instantiateBlockFns(*cs, senv, thisV, strict);
    }

    // Find the matching clause (=== on case expressions, in order), else default.
    int64_t start = -1;
    int64_t defaultIdx = -1;
    for (size_t i = 0; i < sw->clauses.size(); i++) {
        if (auto* cc = dynamic_cast<ast::CaseClause*>(sw->clauses[i].get())) {
            Cpl cv = evalExpr(cc->expression.get(), senv, thisV, strict);
            if (isAbrupt(cv)) return cv;
            if (ts_value_strict_eq_bool(d.v, cv.v)) { start = (int64_t)i; break; }
        } else {
            if (defaultIdx < 0) defaultIdx = (int64_t)i;
        }
    }
    if (start < 0) start = defaultIdx;
    if (start < 0) return normal();

    TsValue* completion = nullptr;
    for (size_t i = (size_t)start; i < sw->clauses.size(); i++) {
        std::vector<ast::StmtPtr>* stmts = nullptr;
        if (auto* cc = dynamic_cast<ast::CaseClause*>(sw->clauses[i].get()))
            stmts = &cc->statements;
        else if (auto* dc = dynamic_cast<ast::DefaultClause*>(sw->clauses[i].get()))
            stmts = &dc->statements;
        if (!stmts) continue;
        for (auto& s : *stmts) {
            Cpl c = execStmt(s.get(), senv, thisV, strict);
            if (c.k == Cpl::Brk && c.label.empty()) return normal(completion);
            if (isAbrupt(c)) return c;
            if (c.v) completion = c.v;
        }
    }
    return normal(completion);
}

Cpl execTry(ast::TryStatement* t, TsMap* env, TsValue* thisV, bool strict) {
    TsMap* tenv = envNew(env, false);
    predeclareLexical(t->tryBlock, tenv);
    instantiateBlockFns(t->tryBlock, tenv, thisV, strict);
    Cpl r = execStmts(t->tryBlock, tenv, thisV, strict);

    if (r.k == Cpl::Thrown && t->catchClause) {
        TsMap* cenv = envNew(env, false);
        if (t->catchClause->variable) {
            Cpl bc = bindPattern(t->catchClause->variable.get(),
                                 r.v ? r.v : jsUndefined(), cenv, thisV, strict,
                                 BindMode::Let);
            if (isAbrupt(bc)) return bc;
        }
        predeclareLexical(t->catchClause->block, cenv);
        instantiateBlockFns(t->catchClause->block, cenv, thisV, strict);
        r = execStmts(t->catchClause->block, cenv, thisV, strict);
    }

    if (!t->finallyBlock.empty()) {
        TsMap* fenv = envNew(env, false);
        predeclareLexical(t->finallyBlock, fenv);
        instantiateBlockFns(t->finallyBlock, fenv, thisV, strict);
        Cpl f = execStmts(t->finallyBlock, fenv, thisV, strict);
        if (isAbrupt(f)) return f;   // finally overrides try/catch completion
    }
    return r;
}

// An if/else branch: a lone FunctionDeclaration is a synthetic one-statement
// block (Annex B.3.2) so it is block-scoped + B.3.3-promoted, not a no-op.
Cpl execBranch(Statement* s, TsMap* env, TsValue* thisV, bool strict) {
    if (!s) return normal();
    Statement* u = unlabel(s);
    if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(u)) {
        TsMap* benv = envNew(env, false);
        instantiateBlockFn1(fd, benv, thisV, strict);
        return execStmt(u, benv, thisV, strict);
    }
    return execStmt(s, env, thisV, strict);
}

Cpl execStmt(Statement* s, TsMap* env, TsValue* thisV, bool strict) {
    if (!s) return normal();

    if (auto* es = dynamic_cast<ast::ExpressionStatement*>(s)) {
        Cpl v = evalExpr(es->expression.get(), env, thisV, strict);
        if (isAbrupt(v)) return v;
        return normal(v.v); // statement completion value (eval result)
    }
    if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(s))
        return execVarDecl(vd, env, thisV, strict);
    if (auto* b = dynamic_cast<ast::BlockStatement*>(s))
        return execBlock(b, env, thisV, strict);
    if (auto* i = dynamic_cast<ast::IfStatement*>(s)) {
        Cpl c = evalExpr(i->condition.get(), env, thisV, strict);
        if (isAbrupt(c)) return c;
        if (ts_value_to_bool(c.v)) return execBranch(i->thenStatement.get(), env, thisV, strict);
        if (i->elseStatement)      return execBranch(i->elseStatement.get(), env, thisV, strict);
        return normal();
    }
    if (auto* w = dynamic_cast<ast::WhileStatement*>(s))
        return execLoopWhile(w, env, thisV, strict, "");
    if (auto* f = dynamic_cast<ast::ForStatement*>(s))
        return execLoopFor(f, env, thisV, strict, "");
    if (auto* fo = dynamic_cast<ast::ForOfStatement*>(s))
        return execForOf(fo, env, thisV, strict, "");
    if (auto* fi = dynamic_cast<ast::ForInStatement*>(s))
        return execForIn(fi, env, thisV, strict, "");
    if (auto* r = dynamic_cast<ast::ReturnStatement*>(s)) {
        Cpl v = r->expression ? evalExpr(r->expression.get(), env, thisV, strict)
                              : normal(jsUndefined());
        if (isAbrupt(v)) return v;
        Cpl ret; ret.k = Cpl::Ret; ret.v = v.v ? v.v : jsUndefined();
        return ret;
    }
    if (auto* br = dynamic_cast<ast::BreakStatement*>(s)) {
        Cpl c; c.k = Cpl::Brk; c.label = br->label; return c;
    }
    if (auto* co = dynamic_cast<ast::ContinueStatement*>(s)) {
        Cpl c; c.k = Cpl::Cont; c.label = co->label; return c;
    }
    if (auto* th = dynamic_cast<ast::ThrowStatement*>(s)) {
        Cpl v = evalExpr(th->expression.get(), env, thisV, strict);
        if (isAbrupt(v)) return v;
        return thrown(v.v);
    }
    if (auto* t = dynamic_cast<ast::TryStatement*>(s))
        return execTry(t, env, thisV, strict);
    if (auto* sw = dynamic_cast<ast::SwitchStatement*>(s))
        return execSwitch(sw, env, thisV, strict);
    if (auto* l = dynamic_cast<ast::LabeledStatement*>(s)) {
        Statement* inner = l->statement.get();
        if (auto* w = dynamic_cast<ast::WhileStatement*>(inner))
            return execLoopWhile(w, env, thisV, strict, l->label);
        if (auto* f = dynamic_cast<ast::ForStatement*>(inner))
            return execLoopFor(f, env, thisV, strict, l->label);
        if (auto* fo = dynamic_cast<ast::ForOfStatement*>(inner))
            return execForOf(fo, env, thisV, strict, l->label);
        if (auto* fi = dynamic_cast<ast::ForInStatement*>(inner))
            return execForIn(fi, env, thisV, strict, l->label);
        Cpl c = execStmt(inner, env, thisV, strict);
        if (c.k == Cpl::Brk && c.label == l->label) return normal(c.v);
        return c;
    }
    if (auto* fd = dynamic_cast<ast::FunctionDeclaration*>(s)) {
        // Top-level fns are instantiated during hoisting (no-op here). A
        // block-level fn has an OWN block-local binding (created at block
        // entry); if its name was promoted (Annex B.3.3, sloppy), copy the
        // current block value to the var-scope target when the declaration
        // statement is reached (R3: written DIRECTLY on the var env).
        if (!strict && !fd->name.empty() && envHasOwn(env, fd->name)) {
            TsMap* vt = envVarTarget(env);
            if (vt && vt->Has(key("\x01x:" + fd->name))) {
                TsValue* val = envGetOwn(env, fd->name);
                if (envIsGlobalVarScope(vt)) {
                    TsValue* ex = nullptr;
                    if (!guardSet(globalThis, boxStr(fd->name), val, &ex)) return thrown(ex);
                } else if (!envIsConst(vt, fd->name)) {
                    vt->Set(key(fd->name), nanbox_to_tagged(val));
                }
            }
        }
        return normal();
    }
    if (auto* cd = dynamic_cast<ast::ClassDeclaration*>(s)) {
        // A class declaration is lexical (bound in the current env, not the var
        // scope) and has an EMPTY completion (does not overwrite the running
        // completion value).
        Cpl c = evalClass(cd->name, cd->baseClass, cd->members, env, thisV, strict);
        if (isAbrupt(c)) return c;
        if (!cd->name.empty()) {
            // Overwrite the TDZ marker predeclareLexical installed.
            env->Set(key(cd->name), nanbox_to_tagged(c.v));
        }
        return normal();
    }
    if (dynamic_cast<ast::InterfaceDeclaration*>(s) ||
        dynamic_cast<ast::TypeAliasDeclaration*>(s))
        return normal(); // type-only

    return unsupported(s->getKind());
}

// --- program entry ----------------------------------------------------------------

Cpl runProgramInEnv(ast::Program* prog, TsMap* env, TsValue* thisV,
                    bool callerStrict) {
    bool strict = callerStrict || prog->isStrict;
    TsMap* penv;
    if (strict) {
        // Strict eval gets its OWN var scope (ES 19.2.1.1 step 12).
        penv = envNew(env, /*fnScope*/true);
    } else {
        penv = envNew(env, false);
    }
    predeclareLexical(prog->body, penv);
    Cpl h = hoistInto(prog->body, penv, thisV, strict);
    if (isAbrupt(h)) return h;
    Cpl r = execStmts(prog->body, penv, thisV, strict);
    if (r.k == Cpl::Ret)
        return throwTyped("SyntaxError", "'return' statement is not allowed in eval code");
    if (r.k == Cpl::Brk || r.k == Cpl::Cont)
        return throwTyped("SyntaxError", "Illegal break/continue in eval code");
    if (r.k == Cpl::Normal && !r.v) r.v = jsUndefined();
    return r;
}

// Root env for indirect eval / Function-constructed code: global scope only.
// var declarations go straight to globalThis.
TsMap* makeGlobalRootEnv() {
    TsMap* root = (TsMap*)ts_map_create();
    root->Set(key("\x01g"), nanbox_to_tagged(jsBool(true)));
    return root;
}

// Builds the interpreted closure for Function(params..., bodySource). Returns
// nullptr with *errOut set (boxed SyntaxError) on parse failure. All
// std::string work happens HERE so the extern "C" boundary frame that
// ts_throw's holds no destructor-owning locals.
void* fctorBuild(const char* paramsUtf8, const char* bodyUtf8, TsValue** errOut) {
    std::string src = "(function anonymous(";
    src += paramsUtf8 ? paramsUtf8 : "";
    src += "\n) {\n";
    src += bodyUtf8 ? bodyUtf8 : "";
    src += "\n})";
    void* h = ts_parse_program(src.c_str(), "<Function>", 0);
    const char* perr = ts_parse_error(h);
    if (perr) {
        *errOut = (TsValue*)ts_error_create_typed("SyntaxError", perr);
        ts_parse_free(h);
        return nullptr;
    }
    auto* prog = (ast::Program*)ts_parse_get_program(h);
    ast::FunctionExpression* fe = nullptr;
    if (prog->body.size() == 1) {
        if (auto* es = dynamic_cast<ast::ExpressionStatement*>(prog->body[0].get())) {
            Expression* inner = es->expression.get();
            if (auto* p = dynamic_cast<ast::ParenthesizedExpression*>(inner))
                inner = p->expression.get();
            fe = dynamic_cast<ast::FunctionExpression*>(inner);
        }
    }
    if (!fe) {
        *errOut = (TsValue*)ts_error_create_typed(
            "SyntaxError", "Function constructor body did not parse to a function");
        ts_parse_free(h);
        return nullptr;
    }
    retainParseHandle(h);
    InterpFn* d = fnDataForFunctionExpr(fe, /*outerStrict*/false);
    d->name = "anonymous";
    return makeInterpClosure(d, makeGlobalRootEnv(), nullptr);
}

TsValue* indirectEvalImpl(const char* src, TsValue** errOut, Cpl* abrupt) {
    void* h = ts_parse_program(src ? src : "", "<eval>", 0);
    const char* perr = ts_parse_error(h);
    if (perr) {
        *errOut = (TsValue*)ts_error_create_typed("SyntaxError", perr);
        ts_parse_free(h);
        return nullptr;
    }
    retainParseHandle(h);
    auto* prog = (ast::Program*)ts_parse_get_program(h);
    Cpl r = runProgramInEnv(prog, makeGlobalRootEnv(), globalThis,
                            /*callerStrict*/false);
    if (r.k == Cpl::Thrown) { *abrupt = r; return nullptr; }
    return r.v ? r.v : jsUndefined();
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry points. These frames hold no destructor-owning locals so a
// ts_throw here is longjmp-safe (longjmp-stdstring rule).
// ---------------------------------------------------------------------------

// Function(body) — used by ts_function_constructor_stub's fallback.
// Returns a boxed interpreted closure or throws SyntaxError.
extern "C" void* ts_interp_function_ctor(const char* bodyUtf8) {
    TsValue* err = nullptr;
    void* fn = fctorBuild("", bodyUtf8, &err);
    if (!fn) ts_throw(err);
    return fn;
}

// Function(p1, ..., pn, body) with n >= 2 pre-coerced string arguments in a
// TsArray (built by ts_function_constructor_args). Joins params with commas,
// assembles, parses, and returns the interpreted closure or throws.
namespace {
void* fctorBuildJoin(TsArray* strArr, int64_t n, TsValue** errOut) {
    std::string params;
    for (int64_t i = 0; i + 1 < n; i++) {
        TsValue* el = ts_array_get_dynamic(boxObj(strArr), ts_value_make_int(i));
        void* sraw = ts_value_get_string(el);
        if (!sraw) continue;
        if (!params.empty()) params += ",";
        const char* u = ((TsString*)sraw)->ToUtf8();
        params += u ? u : "";
    }
    TsValue* bodyEl = ts_array_get_dynamic(boxObj(strArr), ts_value_make_int(n - 1));
    void* braw = ts_value_get_string(bodyEl);
    const char* body = braw ? ((TsString*)braw)->ToUtf8() : "";
    return fctorBuild(params.c_str(), body ? body : "", errOut);
}
} // namespace

extern "C" void* ts_function_ctor_from_strings(void* strArr, int64_t n) {
    TsValue* err = nullptr;
    void* fn = fctorBuildJoin((TsArray*)strArr, n, &err);
    if (!fn) ts_throw(err);
    return fn;
}

// Indirect eval: parse `src` as a Program and run it in global scope.
// Returns the completion value or throws (SyntaxError / whatever the code threw).
extern "C" TsValue* ts_indirect_eval_cstr(const char* src) {
    TsValue* err = nullptr;
    Cpl abrupt;
    abrupt.k = Cpl::Normal;
    TsValue* r = indirectEvalImpl(src, &err, &abrupt);
    if (err) ts_throw(err);
    if (abrupt.k == Cpl::Thrown) ts_throw(abrupt.v);
    return r ? r : (TsValue*)(uintptr_t)NANBOX_UNDEFINED;
}
