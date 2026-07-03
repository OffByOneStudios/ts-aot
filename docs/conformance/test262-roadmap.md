# test262 Conformance Roadmap

**State (2026-07-02, master `60e0d6e1`):** 36,765/45,258 = **81.2%** on the
milestone-tracking scope (sweep tmp_p25: +310 vs p24, 3 traced residuals
from the new-class-expr fix). Stage B (85% = 38,470): 1,705 to go.
15 gated merges this session: ES2025 Set methods (+164), RegExp.escape,
Promise.try + instanceof-Promise offset bug, globalThis fn-decl reflection
(asyncHelpers unblock), `new class {...}()` machinery, async-gen setjmp
ctx-clobber (crash root, 105->34), lazy dstr initializers, for-of OBJECT
assignment patterns (~+188), static private accessor arity, computed
class-field key modvar fallback, dp-matrix drills (descriptor validation,
attr retention, non-writable-length). Wider scope: 36,765/50,506 = 72.8%.
**STAGE A COMPLETE — the 80% milestone (36,206) is MET** (+16 margin at the
confirming sweep, plus ~7 more merged after it: set-path integrity gates +
structural TestIntegrityLevel). +1,159 from the Stage A start at 35,063.

## Where the remaining 14,284 fails live (p23, by category)

| Category | Pass/Total | % | Fails | Character |
|---|---|--:|--:|---|
| language/expressions | 8,770/11,023 | 79.6% | 2,253 | 738 are `dynamic-import` (structural); class 344, object 167, compound-assign 123, super 73 |
| language/statements | 7,627/9,154 | 83.3% | 1,527 | class 575, for-of 212, for-await-of 208 (#44 crash), with-residue 103, function 94 |
| intl402 | 268/1,566 | 17.1% | 1,298 | Intl.* plumbing; ICU already bundled — dedicated Stage C effort |
| staging | 521/1,632 | 31.9% | 1,111 | sm-shell harness bits, Float16Array, explicit-resource-management |
| annexB | 351/1,079 | 32.5% | 728 | legacy web-compat (function-in-block hoisting, HTML comments, substr-era) |
| built-ins/Array | 2,404/3,075 | 78.2% | 671 | prototype 510, fromAsync 95, from 26 |
| language/module-code | 1/583 | 0.2% | 582 | structural (AOT single-image; no runtime module records) |
| built-ins/RegExp | 1,254/1,822 | 68.8% | 568 | prototype 164, modifiers 70, unicodeSets 50, named-groups 33, escape 20 |
| built-ins/Object | 2,919/3,410 | 85.6% | 491 | defineProperty 146 + defineProperties 62 (dp-matrix), prototype 77, gOPD 28 |
| built-ins/TypedArray(+Ctors) | 1,328/2,162 | 61.4% | 834 | BigInt-TA storage tier-2 (#34), detached-buffer semantics, ctor internals |
| built-ins/String | 836/1,212 | 69.0% | 376 | prototype 329 (locale/regexp-coupled methods) |
| built-ins/Atomics | 1/376 | 0.3% | 375 | N/A — no shared memory (worker_threads stance) |
| language/eval-code | 14/347 | 4.0% | 333 | structural (no runtime code generation) |
| built-ins/DataView | 318/550 | 57.8% | 232 | mostly detached/OOB + BigInt getters/setters |
| built-ins/Function | 296/509 | 58.2% | 213 | dynamic `Function()` structural core (~79); residue: bind/length/name edges |
| built-ins/Set | 171/381 | 44.9% | 210 | ES2025 set methods (union/intersection/difference…) largely missing |
| built-ins/Promise | 461/631 | 73.1% | 170 | species/subclass residue, resolve-thenable edges |
| built-ins/Proxy | 142/311 | 45.7% | 169 | per-trap invariant matrices (ownKeys now done — pattern to replicate) |
| built-ins/Temporal | 4,028/4,165 | 96.7% | 137 | IANA tz database (#33), relativeTo edges (#32) |
| built-ins/SharedArrayBuffer | 0/104 | 0% | 104 | N/A — no shared memory |
| ShadowRealm / Disposable*Stack / AbstractModuleSource | 0/176 | 0% | 176 | unimplemented proposals (Disposable* = explicit-resource-management, plausible Stage C) |
| everything else | — | — | ~1,750 | diffuse: Iterator 119, Date 113, literals 117, JSON 93, NativeErrors 69, Symbol 46, … |

Structural / architectural N-A within the sweep: dynamic-import 738 +
module-code 582 + eval-code 333 + import 85 + import.meta 21 + dynamic
Function/Generator ctors ~115 + Atomics 375 + SAB 104 ≈ **2,350 tests** →
practical ceiling ≈ 95–96% of the full 50.5k scope.

## Stage A → 80% — COMPLETE (2026-07-02)

All five line items landed (`with`, NewTarget/subclass-builtins, Promise,
Function, Proxy traps) plus the last-mile closers: Proxy ownKeys ES 10.5.11
invariants + Object.keys enumerable post-filter, flat-object IN-PLACE integrity
flags (preventExtensions/seal previously demoted to a detached TsMap copy),
exotic-object integrity side-table (functions/closures/arrays/Dates) with
set-path gates, structural TestIntegrityLevel, and `arguments[N]` padding for
object-literal generator/async-generator methods. History in the git log and
memory checkpoints (`stage-a-*`, `roadmap-phases-1-5`).

## Stage B → 85% (38,470 on the 45,258 scope; +2,248) — volume families

Ordered by (size × coherence). Each is a measure-first cluster drill.

Characterizations below VERIFIED by reason-clustering p23 (2026-07-02);
"uses eval" counts are permanently unwinnable and already subtracted from the
winnable notes.

| Vein | Fails | Verified character |
|---|--:|---|
| class residue | 921 | **elements 438** is the anchor (field/private/accessor semantics), dstr 120, subclass 72; minus 124 `uses eval` → ~800 winnable. Top reasons: missing TypeError 59, missing ReferenceError 55, 49 crashes (no message) |
| TypedArray + Ctors + DataView + ArrayBuffer | 1,131 | anchor: BigInt-TA storage tier-2 (#34, 5-layer); detached-buffer TypeErrors are a big shared root (not re-verified this pass) |
| for-of + for-await-of | 422 | **102 hard CRASHES (VectoredException)** — #44's cluster is bigger than its ~76 estimate; dstr 156, `.then` on undefined 55. Crash roots are historically the highest-value drills |
| Array/prototype | 511 | method-spread, NOT species-centric: concat 46, lastIndexOf 39, sort 38, indexOf 33, splice 29 — shared roots look like ToObject/array-like coercion + missing TypeErrors (38) + holes; cross-realm 10 unwinnable |
| String/prototype | ~366 | abrupt-completion/coercion-order protocol (63 expected-Test262Error), regexp-coupled subset split/match/matchAll/replace ≈ 87, trim family 50; 10 eval |
| RegExp families | 337 | **103 are parser-gated "unsupported feature"** (modifiers 70, escape 20, duplicate-named-groups 13) — greenfield; unicodeSets/generated 50 real fails; 13 compiler Driver ERRORS (investigate — possible crash root); prototype residue 164 |
| Set methods (ES2025) | 210 | CONFIRMED greenfield: 184/210 are literally "unsupported feature: set-methods" (union/intersection/difference/…) — spec-mechanical, predictable yield |
| dp-matrix (defineProperty/ies) | 208 | real ValidateAndApplyPropertyDescriptor: redefinition TypeErrors, per-attr transitions; unlocks parts of Array/TypedArray matrices too |
| Promise residue | 170 | **12 CRASHES**, promise-try 12 (unimplemented proposal), builtin fn length/name/order metadata 21, any 28 + allSettled 20 + prototype 45 |
| Proxy invariant matrices | 169 | replicate the ownKeys invariant pattern per trap (get/set/defineProperty/gOPD) |
| compound-assign + super + expressions/object | 363 | compound-assign = reference-semantics matrices (PutValue on private refs/no-setter TypeErrors ~24) + 20 eval; super/object not yet drilled |

## Stage C → 88–90% — deep subsystems

intl402 (1,298; ICU bundled, needs Intl.* object plumbing), annexB (728,
mechanical legacy semantics), staging (1,111; sm-shell harness + Float16Array),
explicit-resource-management / Disposable*Stack (104+52), IANA tz (#33) +
Temporal residue (137), Iterator helpers residue (119), mapped-arguments
aliasing (written off unless an accessor-backed arguments redesign happens).
The compile-time module linker is now scoped as
`docs/tickets/CONF-P3-module-linker-dynamic-import.md` (task #60): closed-world
specifier registry + per-module lazy init → namespace/live bindings → module-goal
execution. Realistic yield ~700–800 (405 of the dynamic-import fails are separate
proposals: source-phase-imports/import-defer/import-attributes, out of scope).

## Operating discipline (non-negotiable)

Measure-first cluster drill → branch → probe battery → gates (golden-ir 267/279,
node 295/297, 2k `.gate_base.pkl` 0-lost, plus a cluster-specific baseline like
the BigInt-TA 367-list when touching that area) → full-sweep diff vs the previous
results-jsonl → merge `--no-ff`. Two separate `cp` commands for the DLL
(multi-dest `cp` treats the middle dir as a source). Full `cmake --build` when
runtime symbols change. Never build during a running sweep. tmp/*.exe probes can
bind stale static runtime — `TS262_SHARED_RUNTIME=1 run_test262.py --filter X -j1`
is authoritative.

## Accepted limitations (do not re-attempt without new architecture)

`eval` / runtime `dynamic-import` / module-code (AOT single image), dynamic
`Function()`/`GeneratorFunction()` constructors, Atomics/SharedArrayBuffer (no
shared memory), private-name per-class identity (6 `private-setter-shadowed-by-*`),
monkey-patched `Date.prototype.toString` ToPrimitive (1 sm test), mapped-arguments
aliasing (~26, archaic).
