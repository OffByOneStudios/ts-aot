# EVAL-001: `eval` / `Function` constructor support via a tree-walking interpreter

**Status:** IN PROGRESS 2026-07-04 — Phases 0-2 MERGED (parser runtime-linkable
e88d0cbd; interpreter core + Function(params...,body) 45c98e17, Function family
+24/0 lost); Phase 3 (global eval() wiring + runner un-gating) implemented,
full-sweep measurement running. Phase 4 = residue grind. Original design
2026-06-17; refinement §9-10.
**Author:** design investigation 2026-06-17; refinement 2026-07-04.
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

---

## 9. Refinement 2026-07-04 — what the last two weeks already built

Re-verified against master (merge 114, baseline tmp_p52 = 38,333 / 87.34%).
Two of the original cost items now EXIST, built for other reasons, and the
prize pool is ~3x the original estimate.

### 9.1 Exception handling — DONE (was 1-2 wks)
The setjmp handler-stack + `ts_throw` subsystem is live in the runtime
(`Core.cpp` — handler contexts even save/restore `saved_with_depth` on
unwind). The interpreter's try/catch/finally pushes the SAME handler frames
the AOT codegen uses; §6's "reusable dividend" was built from the other
direction (IteratorClose landed in roadmap phase 4). Residual work: walker
integration only (~2-3 days). Known rule: `ts_throw` pops its own handler —
never pop in the landing branch.

### 9.2 Runtime object-environments — precedent EXISTS (`with`)
The `with` implementation added a runtime scope stack with bare-identifier
routing (`ts_with_push/pop`, `ts_with_set` in `TsGlobals.cpp:6909+`, GC-rooted
per the unscanned-container rule). This is exactly the shape of the
interpreter's environment record. Design decision (new): **env records are
TsMap-backed GC objects**, chained via a `parent` slot —
- bindings are automatically GC-visible (no bespoke rooting for locals);
- `let`/`const` TDZ uses the existing `NANBOX_TDZ` sentinel (0x9);
- the only extra rooting is the interpreter's C++ temporaries → one shadow
  value-stack registered via `ts_gc_register_scanner` + minor fixup.

### 9.3 Global wiring — mostly DONE
Since 2026-06-17: global function declarations are own properties of
globalThis; bare-identifier reads resolve through the global object;
`Program::isStrict` is stamped by the PARSER (strict propagation into eval'd
source is a flag pass-through); real TDZ exists. §4's "eval/Function/global
wiring 2-3 wks" shrinks to mostly SyntaxError-timing + `var`-into-global
semantics.

### 9.4 Interim stub to replace
`ts_function_constructor_stub` (Primitives.cpp ~1526) pattern-matches literal
idioms ('return this', the subclass idiom from merge 105). Phase 3 replaces
it outright: parse-then-interpret, EvalError never thrown for valid source.
Note the harness dividend: resizableArrayBufferUtils.js-style includes that
died on `new Function(...)` un-gate entire families.

### 9.5 Updated prize pool (from tmp_p52.jsonl)
| Family | fail | skip (eval-gated) |
|---|---|---|
| language/eval-code | 160 | 656 |
| built-ins/Function | 84 | 137 |
| annexB (incl. indirect-eval set) | 336 | 325 |
| language/function-code | 140 | 29 |

Direct-eval/caller-scope and realm tests stay N/A, but the winnable
indirect-eval + Function-ctor + un-gated-harness slice is realistically
**~350-500 tests** (vs the original ~100-150).

### 9.6 Revised estimate
| Component | Was | Now |
|---|---|---|
| Parser runtime-linkable | 3-5 d | 3-5 d (unchanged) |
| Tree-walker core | 2-3 wk | 2-3 wk (unchanged) |
| GC integration | 1 wk | ~3 d (env-as-TsMap; shadow stack only) |
| Exceptions | 1-2 wk | 2-3 d (subsystem exists) |
| eval/Function/global wiring | 2-3 wk | ~1 wk (globals/strict/TDZ done) |
| **Total** | 6-10 wk | **~4-6 wk** |

## 10. Work plan (phased, each phase gate-battery merged)

- **Phase 0 — parser runtime-linkable (3-5 d).** Move Lexer/Parser/AstNodes
  into a lib the runtime links (start always-linked static; lazy `tsinterp`
  DLL is a later size optimization — binary is already ~39MB). `extern "C"
  ts_parse_program(const char* src, int strict, ...)` returning an opaque
  AST handle; convert parser `std::runtime_error` → JS SyntaxError AT the
  eval boundary (longjmp-safety: no std::string locals in the frame that
  calls ts_throw — split per the longjmp-stdstring rule). Gate: node +
  golden-ir + 2k unchanged (+0 honest merge — ABI prerequisite).
- **Phase 1 — walker core, expressions + statements (2-3 wk).** New
  `src/runtime/src/interp/` walking `ast::*` directly. EVERY operation
  delegates to the ts_* ABI (ts_add/ts_value_* dispatchers,
  ts_object_get_prop_v/ts_object_set_dynamic, ts_call_with_this_N,
  ts_iterator_get/close, coercion helpers) — the walker adds ONLY control
  flow + env plumbing. Env records per §9.2. Function expressions in eval'd
  code close over interpreter envs (an interpreted-closure TsClosure variant
  whose invoke trampolines back into the walker). Completion values (eval
  returns the statement completion). Defer: generators/async in eval'd code
  (throw a clear TypeError; banked).
- **Phase 2 — exceptions (2-3 d).** try/catch/finally via the existing
  handler stack; walker unwinding cooperates with ts_throw longjmp;
  break/continue/return as C++-level completion records, NOT longjmp.
- **Phase 3 — eval/Function wiring (~1 wk).** Replace
  ts_function_constructor_stub: Function(p1..pn, body) assembles source,
  parses (SyntaxError at construction time), returns an interpreted closure
  whose .prototype/length/name are spec-correct. Indirect eval entry
  (global scope, caller-strictness flag, var→globalThis, let/const in a new
  eval-scope). Direct eval: degrade to indirect semantics where the test
  only needs global access; document caller-scope reads as N/A.
- **Phase 4 — test262 grind (1-2 wk).** Un-skip eval-gated families
  (eval-code 656 skips, annexB indirect-eval ~160/325, Function 137),
  fresh sweep, then measure-first-drill the residue per cycle. Success bar:
  +350 net, 0 lost, all gates green each merge.

Open items carried: lazy-DLL packaging (later); interpreter frames vs the
precise GC stack-walk at the AOT↔interp boundary (marshalling is TsValue;
the shadow stack in §9.2 is the scanned root — verify under TS_GC_NURSERY=0
early in Phase 1).

## 11. Phase 4a diagnosis (2026-07-09) — the residue's dominant blocker

Probed the remaining eval-pathed failures (523 in baseline; annexB/language/
eval-code = 177 of them, direct 129 / indirect 48). Three representative
probes (func-if-stmt-else-decl-eval-func-block-scoping, ...existing-var-no-init,
global-block-decl-eval-global-existing-var-update) all fail the same way and
it is NOT a walker bug — the walker's EvalDeclarationInstantiation + Annex
B.3.3.3 promotion machinery is present and runs.

**Blocker: AOT global vars and globalThis are not unified.** The test files
declare `var f = 123` / `var initialBV` in the AOT-COMPILED program; eval'd
code (correctly) writes `f`/`initialBV` through globalThis; but compiled code
reads/writes its own LLVM global slot. Writes from eval are invisible to the
compiled reader and vice versa. Function declarations were already unified
(§9.3: own properties of globalThis); `var` bindings were not.

**Fix direction (compiler-side, standard engine practice): eval-taint
deopt.** If a module contains any call whose callee is the identifier `eval`
(or the module uses `Function` constructor patterns needing it), lower that
module's toplevel `var` declarations to globalThis-backed storage: declaration
= CreateGlobalVarBinding on globalThis; every read/write of those names =
globalThis property get/set (the bare-identifier-through-global path already
exists). Non-eval modules keep fast LLVM global slots — zero cost for normal
code. Scope: Analyzer taint flag + ASTToHIR global-binding lowering switch.

Estimated unlock: most of annexB/eval-code 177 + a similar share of
language/eval-code and function-code residue — plausibly 250-350 of the 523.
Walker-internal residue (if any) only becomes measurable after this lands.
