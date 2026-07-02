# test262 Conformance Roadmap

**State (2026-07-01, master `9d0627ce`):** 35,063/45,258 = **77.5%** (5,109 skipped
on unsupported flags/features). Structural floor: ~550 tests are AOT-unreachable
(`eval` 256, runtime `dynamic-import` 292) → theoretical ceiling ~96–97%.

Landed this cycle: operator coercion (+433), strict-write TypeError (+44),
BigInt-as-primitive (+54), class by-classname (+51), IteratorClose, TDZ sentinel +
let/const closure capture. See `docs/conformance/*.md` and the git log.

## Stage A → 80% (+1,143) — mapped coherent roots

| Item | Est. | Starting point |
|---|--:|---|
| ~~`with` statement~~ **LANDED 2026-07-02** (`1c8c6656`) | ~100 | with-cluster 20→31/146; reads/writes(two-phase lref)/delete/throw-unwind all in; plus eval identity stub + Promise combinator IsConstructor. Residue: with-resolved method-call `this` |
| ~~NewTarget / subclass-builtins~~ **LANDED 2026-07-02** (`1fc1c182`, `35f14091`) | ~150 | subclass-builtins 0→68/70; new.target 2→11/14 (ambient swap-register); **class-expression prototype install fixed** (`const A = class {}` had no A.prototype). Residue: NativeError message/super families, AggregateError drill (~17), WeakRef getter |
| ~~Promise cluster~~ **LANDED 2026-07-02** (`eac1c4d0`, `f1a05daa`) | ~150/334 | 339 cluster fails → 180 pass. Roots: GENERIC deferred-callee bug (`var f; closure sets f; f()` called undefined), GENERIC nested-fn tryDepth leak (closure return popped caller's handler), NewPromiseCapability receiver protocol (streaming + IteratorClose), resolve-function spec steps |
| Function cluster | ~120/267 | `.name`/`.length` descriptors, bind edges |
| Proxy trap completion | ~120/243 | Missing traps (defineProperty, getPrototypeOf, construct…) — mechanical trap-by-trap |
| Small roots | ~50 | close-on-throw, strict residue, enum order, Temporal Group B edges |

## Stage B → 85% (+3,406 total) — volume families

TypedArray/Buffer (937: BigInt-TA static-store, detached semantics, ctor internals),
class residue (845; much unlocks after NewTarget), async-iteration (352, incl. the
harness-emergent dstr crash family), staging/sm (804: needs Float16Array + sm-shell
bits), annexB (296), diffuse language tail.

## Stage C → 88–90% — deep subsystems + the grind

intl402 (565: Intl.NumberFormat/DateTimeFormat plumbing — ICU already bundled),
IANA tz + Temporal residue (~200), mapped arguments (154), diffuse built-ins tail
(2,938 — autoloop territory).

## Operating discipline (non-negotiable)

Measure-first cluster drill → branch → probe battery → gates (golden-ir 267/279,
node 295/297, 2k `.gate_base.pkl` 0-lost, plus a cluster-specific baseline like the
BigInt-TA 367-list when touching that area) → full-sweep diff vs the previous
results-jsonl → merge `--no-ff`. Two separate `cp` commands for the DLL (multi-dest
`cp` treats the middle dir as a source). Full `cmake --build` when runtime symbols
change.

## Accepted limitations (do not re-attempt without new architecture)

`eval` / runtime `dynamic-import` (AOT), private-name per-class identity (6
`private-setter-shadowed-by-*`), monkey-patched `Date.prototype.toString`
ToPrimitive (1 sm test), Atomics/SharedArrayBuffer (no shared memory).
