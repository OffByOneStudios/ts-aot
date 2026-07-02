# test262 Conformance Roadmap

**State (2026-07-02, master `b3c8e11d`):** 35,712+/45,258 = **78.7%** (sweep tmp_p18;
+649 from the Stage A start at 35,063). All five Stage A line items have landed.
The 80% milestone (36,206) is ~490 tests away — the remaining distance comes from
the mapped "small roots" and early Stage B veins (defineProperty descriptor
semantics 277, Array.prototype 568, class residue 449).
Structural floor: ~550 tests are AOT-unreachable (`eval` 256, runtime
`dynamic-import` 292) → theoretical ceiling ~96–97%.

Landed this cycle: operator coercion (+433), strict-write TypeError (+44),
BigInt-as-primitive (+54), class by-classname (+51), IteratorClose, TDZ sentinel +
let/const closure capture. See `docs/conformance/*.md` and the git log.

## Stage A → 80% (+1,143) — mapped coherent roots

| Item | Est. | Starting point |
|---|--:|---|
| ~~`with` statement~~ **LANDED 2026-07-02** (`1c8c6656`) | ~100 | with-cluster 20→31/146; reads/writes(two-phase lref)/delete/throw-unwind all in; plus eval identity stub + Promise combinator IsConstructor. Residue: with-resolved method-call `this` |
| ~~NewTarget / subclass-builtins~~ **LANDED 2026-07-02** (`1fc1c182`, `35f14091`) | ~150 | subclass-builtins 0→68/70; new.target 2→11/14 (ambient swap-register); **class-expression prototype install fixed** (`const A = class {}` had no A.prototype). Residue: NativeError message/super families, AggregateError drill (~17), WeakRef getter |
| ~~Promise cluster~~ **LANDED 2026-07-02** (`eac1c4d0`, `f1a05daa`) | ~150/334 | 339 cluster fails → 180 pass. Roots: GENERIC deferred-callee bug (`var f; closure sets f; f()` called undefined), GENERIC nested-fn tryDepth leak (closure return popped caller's handler), NewPromiseCapability receiver protocol (streaming + IteratorClose), resolve-function spec steps |
| ~~Function cluster~~ **LANDED 2026-07-02** (`6b32cdf4`, `cebcd5ca`) | ~120/267 | Winnable subset 221 (79 are dynamic-Function structural): 17→98. @@hasInstance, bound-fn name/length, caller/arguments %ThrowTypeError%, constructor backrefs |
| ~~Proxy trap completion~~ **LANDED 2026-07-02** (`3f39b8b4`, `b3c8e11d`) | ~120/243 | 6 missing internal-method traps wired at Reflect + Object layers (274 fails → ~46 pass net after trap-less-proxy guards). Remaining: ownKeys invariants (24), set/has/construct residue |
| Small roots | ~50 | Next mapped root: **Object.defineProperty/defineProperties descriptor semantics (277 fails)** — redefinition TypeErrors, configurability enforcement, accessor descriptors. Then: NativeError message/super (~17), AggregateError (~17), WeakRef getter (4) |

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
