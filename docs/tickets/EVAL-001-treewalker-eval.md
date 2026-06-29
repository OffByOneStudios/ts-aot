# EVAL-001: `eval` / `Function` constructor support via a tree-walking interpreter

**Status:** Parked — revisit after test262 conformance plateaus.
**Author:** design investigation 2026-06-17.
**TL;DR:** Reclaim the *indirect*-eval + `Function`-constructor test262 cluster
(~100-150 tests) with a small tree-walking interpreter embedded in the runtime
that **delegates to the existing `ts_*` runtime ABI**. ~6-10 focused weeks.
A bytecode VM doubles the work for zero extra conformance here, and ORC JIT is
strictly worse for eval. Direct eval with caller-scope access stays N/A.

---

## 1. The core constraint

The eval'd string only exists **at runtime, inside the generated executable**.
The host compiler (`ts-aot.exe`) is not present in the produced binary. So the
evaluator must live in the **runtime** (linked into the exe), not the compiler.

Two consequences:
- The native parser must become **runtime-linkable** (it currently builds only
  into the host compiler lib).
- Whatever executes the parsed AST also lives in the runtime.

## 2. Parser runtime-linkability — EASY (investigated)

The native C++ parser (`src/compiler/parser/Lexer.{h,cpp}`,
`Parser.{h,cpp}`, `src/compiler/ast/AstNodes.h`, `AccessModifier.h`) is
self-contained:

- **Dependencies:** std (`string/vector/memory/variant/...`) + `fmt` + `spdlog`
  only. **No LLVM, no analyzer, no compiler internals.** `fmt`/`spdlog` are
  already linked into the runtime.
- **Re-entrant:** the only statics are pure helper *functions* (`decodeUtf8`,
  `collectBoundIdentNames`, unicode classifiers) — no mutable global state.
  Nested / recursive eval is safe; each `Parser` instance is independent.
- **Clean API:** `std::unique_ptr<ast::Program> Parser::parse(const std::string&)`.
  AST nodes are `unique_ptr`-owned (self-freeing, transient, **never GC
  objects**) — the collector is not involved with the AST.
- **Size:** ~7,400 LOC (Parser 4,496 / Lexer 1,932 / AstNodes 938).

**Step 1 is packaging, not a rewrite (~3-5 days):** move those files into a lib
the runtime links (or a lazily `LoadLibrary`'d `tsinterp` DLL), wrap `parse()` in
an `extern "C"` entry, and convert the parser's `std::runtime_error` syntax
failures into a JS `SyntaxError` at the eval boundary. The R1 parser-push early
errors come along for free (most `expected parse error` cases already handled).

## 3. The real ceiling is scope, not the evaluator

The runtime has **no environment/scope objects** — locals are SSA values / stack
slots chosen by codegen. This splits eval cleanly:

- **Indirect eval** (`(0,eval)(s)`, `var e = eval; e(s)`, `new Function(...)`)
  runs in **global scope only** — no lexical capture. The evaluator needs only
  the global object. **Tractable — this is the target.**
- **Direct eval** (`eval(s)` reading/writing the *caller's* locals, declaring
  vars in the caller's scope) requires materializing every direct-eval-containing
  function's locals into a heap **environment record** the evaluator can reflect
  on — a pervasive codegen change (detect at compile time, heap-allocate the env,
  route ALL local access through it, kill the SSA fast path). V8-style deopt
  machinery. **N/A — same bucket as `with`.** A JIT does NOT help here; only
  codegen does.

The interpreter materializes *eval'd code's own* locals in its own frames (fine);
the boundary it cannot cross is reading the AOT caller's SSA locals.

## 4. Chosen approach — tree-walker with shared-ABI delegation

Walk the parsed AST directly; the C++ recursion stack *is* the VM stack. The one
design rule that makes this sane:

> **Every AST node that performs a JS operation calls the SAME `ts_*` runtime
> function the AOT codegen already emits** — `ts_add`, `ts_call_with_this_N`,
> `ts_object_get_prop_v` / `ts_object_set_dynamic`, `ts_iterator_get`,
> `ts_to_number`, `ts_throw`, the well-known-symbol/coercion helpers, etc.

So the interpreter is a **dispatcher**, not a second implementation of the
language. Semantics (arithmetic, coercion, property lookup, calls, iteration,
exceptions) stay single-sourced in the runtime. The only new surface is
control-flow structure + value/scope plumbing. Without this discipline you
recreate the audit's worst "divergent multi-site impl" smell at maximum scale —
do not hand-roll operators in the walker.

### Component effort (indirect eval, generators/async deferred)

| Component | Notes | Estimate |
|---|---|---|
| Parser runtime-linkable | §2 | 3-5 days |
| Tree-walker core | AST eval over `ts_*`; frames + upvalue capture for eval'd closures | 2-3 wks |
| GC integration | root the interpreter value-stack + frames; write barriers (unscanned-container rule) | ~1 wk |
| Exception handling | a try/catch handler stack with **setjmp landing pads** bridging `ts_throw` (longjmp) | 1-2 wks |
| eval/Function/global wiring | global-object access, strict-mode rules, SyntaxError timing, test262 grind | 2-3 wks |
| **Total** | no direct-eval-scope, no generators/async | **~6-10 wks** |

Coverage target: the `Function`-ctor cluster (~101) + indirect-`eval` tests
(~100-150 total winnable; the broader ~430 OOS eval/with/realm set is mostly
direct-eval/`with`/realm and stays N/A).

## 5. Why not bytecode VM / ORC JIT

- **Bytecode VM:** same test262 coverage but adds the biggest line item — a
  bytecode format + AST→bytecode compiler (3-5 wks) — for **~3-4 months total**.
  Its only wins are execution speed (10-50x) and suspendability
  (generators/async). For eval — dynamic, cold, rarely hot — neither matters.
  Reach for it only for a *non-eval* reason (a general interpreted fallback tier,
  or common eval'd generators — both unlikely).
- **ORC JIT (reuse the AOT pipeline in-process / lazy `tsjit.dll`):** one source
  of semantics and native speed, BUT ~50-100MB LLVM even as a side DLL,
  ORC-emitted frames need **stackmaps registered with the precise GC** at JIT
  time, per-eval **cold-start** is slow (parse + LLVM compile each call), and
  eval'd dynamic code runs the boxed/`any` path anyway → no speed win to justify
  the weight. Strictly worse than the tree-walker for eval.

## 6. Reusable dividend

The **setjmp try-scope** primitive built for §4 exceptions is exactly the missing
piece behind the collection-constructor `IteratorClose` crashes (the
`new Map(badIterable)` cluster — catch a thrown `set()`/`add()`, call the
iterator's `return()`, rethrow). Building it for eval pays off in the
`Map`/`Set`/`WeakMap`/`WeakSet` constructors regardless. See the crash-cluster
taxonomy.

## 7. Phased plan

1. **Parser runtime-linkable** (`tsinterp` lib/DLL, `extern "C" ts_parse`,
   SyntaxError marshalling). Prerequisite for any approach.
2. **Tree-walker, indirect eval + `Function` ctor**, global scope, shared-ABI.
   Lazily loaded so non-eval binaries pay nothing.
3. **Direct eval / caller-scope:** N/A (document like `with`) unless a real
   product need appears — and even then it's a codegen project, not a VM one.

## 8. Open questions for revisit

- Lazy-load packaging: `LoadLibrary`/`dlopen` a `tsinterp` module on first eval
  vs. always-linked. Lazy keeps non-eval exes lean (the binary is already ~39MB
  static; see the large-static-binary note).
- Strict-mode propagation into the eval'd program (the caller's strictness
  affects the eval'd code's early errors and `var` introduction rules).
- How the interpreter's frames present to the precise GC stack-walk at the
  AOT↔interpreter boundary (marshalling is via `TsValue`, which is clean, but
  the live interpreter stack must be a scanned root).
