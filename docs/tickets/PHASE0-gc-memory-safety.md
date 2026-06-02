# PHASE 0 — GC / Memory Safety (the production gate)

**Status:** Active — 2026-06-02
**Target:** General Node replacement (server + CLI; broadest scope).
**Sequence decision:** GC correctness is the FIRST focus (chosen 2026-06-02). No
conformance / perf / tooling work gates on this, but a moving-GC or rooting bug
silently corrupts data nondeterministically, so this is the existential blocker
for a long-running runtime. Everything in `docs/PRODUCTION-ROADMAP.md` sequences
behind it.

This ticket is the detailed plan referenced by the roadmap's Phase 0. It extends
and partially subsumes [GC-001](GC-001-verification-harness-and-moving-gc-fix.md).

---

## What is already DONE (do not redo)

- **Verification harness exists and is green** (`tests/gc/`, 15 programs × default/
  nursery0/verify2). Differential (moving vs `TS_GC_NURSERY=0`) byte-identical;
  INV-1 (`TS_GC_VERIFY=2`) clean. Builtins: `__ts_gc_minor/major/collect/verify`.
- **Cross-generational write barrier PROVEN clean** (`cb83b6b2`): 4 matrix programs
  (object field / array element / Map value / closure cell) tenure a holder, do an
  old→young store, minor-GC, verify — all green. The card table is correct for every
  holder type.
- **The libuv-struct-in-GC-memory rooting-gap class is CLOSED** (`823dfabf`,
  `adc8b7a3`, `34f05859`): 11 sites (8 request structs + 3 fs watcher handles) moved
  off the GC heap to malloc, freed in their libuv callbacks, with carried GC pointers
  rooted. Was a 100% stream-test crash under `TS_GC_STRESS=1` → now 8/8.
- **The moving-GC corruption (BUG 4/6/7) is UNREPRODUCED on the current build** and,
  per accumulated evidence (lodash NURSERY=0 == default; harness green incl. cross-gen),
  most likely already fixed by prior closure-cell + builtin-tenuring work. We do NOT
  have a deterministic single-forced-GC repro.

**Implication:** the remaining gate work is (a) finish a small set of known rooting
gaps, (b) make memory-safety *provable in CI*, and (c) decide and land the formal
cure so the moving GC is correct-by-design rather than correct-by-absence-of-repro.

---

## Remaining Phase 0 work (sequenced)

### 0.1 — Async work-item promise rooting — **S**
Malloc'd async *work* structs hold an unrooted GC `work->promise` (e.g. fs
`WriteFileWork`/`FSFdAsyncWork`, crypto pbkdf2/scrypt/keygen). The struct is fine
(malloc), but the promise inside is invisible to the GC for the op lifetime — a
fire-and-forget `fs.promises.writeFile(...)` (no await/then) can have its only
promise reference be that malloc slot → collectible under pressure.
- Per site: `ts_gc_register_root(&work->promise)` on dispatch, unregister in the
  after-work callback. Verify each site doesn't already keep the promise reachable
  before assuming a bug.
- **Exit:** a fire-and-forget async-op stress test stays clean under `TS_GC_STRESS=1`.

### 0.2 — C++ container GC-pointer audit — **M**
Per runtime-safety.md mandate. Grep `src/runtime` + `extensions` for
`std::vector|std::function|std::deque|unordered_map` holding `TsValue`/`Ts*`-pointer
types; each needs a mark-scanner + minor-fixup (or move to `ts_alloc`/`TsArray`/`TsMap`,
or `ts_gc_register_root` a stable slot). Already-rooted exemplars: microtask queue,
promise callbacks, string caches, process handler vectors.
- **Exit:** a documented audit list; every holder either rooted or proven not to hold
  GC pointers across a GC.

### 0.3 — Precise GC statepoints (the formal cure) — **M (re-estimated from L)** — CHOSEN PATH
The user chose precise roots over blanket tenuring. Infrastructure already exists in
`src/compiler/codegen/CodeGenerator.cpp`: LLVM 18 RS4GC integration, gc.relocate
index-fixing (RS4GC off-by-one), the addrspace(1)-is-a-GC-VALUE model, FastISel
disabling under statepoints. The job is to FINISH and VALIDATE it, then make it default.

**2026-06-02 MEASUREMENT — the path is ~98.5% done, re-estimated L→M.** `--gc-statepoints`
already works end-to-end: a smoke program reports `[StackMap] Parsed 61 safepoints with 44
GC root locations` + `[GCRoots] Precise root pushing enabled` and runs correctly — minor GC
IS consuming precise stackmap roots today. Measured breadth:
- **gc-suite: 15/15 PASS** under statepoints.
- **golden_ir statepoints-vs-default differential: 266 MATCH / 2 MISMATCH / 0 CRASH / 2
  COMPILE-ERROR** of 268 working programs (11 base-skipped were already broken w/o statepoints).

The remaining gap is a concrete worklist, not a rebuild:
1. ~~**Boolean closure-cell addrspace ABI bug (2 CE)**~~ — **FIXED `90ffbee5`.** Root cause
   was NOT addrspace but a signature inconsistency: the function-trampoline return path
   (HIRToLLVM.cpp:8392) was the lone site boxing an i1 return via `ts_value_make_bool(i64)`
   while the canonical helper + ~20 sites use `(i32)`. Opaque pointers tolerate it in the
   default build, but RS4GC re-derives the callee signature from the `(i64)` declaration →
   every `i32` call fails the verifier. Unified the trampoline on `(i32)`. Both programs now
   compile + run identically in both modes; 0 default regressions; gc-suite 15/15 statepoints.
2. **Safepoint-relocation bug (2 MISMATCH):** `test_proxy_reflect.ts`,
   `test_promise_withResolvers.ts` produce wrong output under statepoints — a GC value not
   relocated/preserved across a safepoint in the proxy/promise paths. Root-cause via IR diff
   around the safepoint.
3. **Diagnostic spam:** `[StackMap]`/`[GCRoots]` print to stdout — env-gate (e.g. behind
   `TS_GC_DEBUG`) before default; they currently pollute program output.

After 1–3: extend the differential to node + a test262 slice, then flip the default behind
the harness gate.
- Make **minor GC consume the LLVM stackmap** as its precise root set (today the minor
  collector relies on conservative stack scan + the manual `ts_gc_register_root` set;
  statepoints give exact, relocatable roots).
- Resolve the addrspace(1) ABI consistently (user→user calls stay addrspace(1) and are
  rooted; runtime-boundary calls cast (1)→(0) and back) — the CodeGenerator already
  models this; verify no GC pointer crosses a safepoint un-relocated.
- Roll out incrementally **with the harness as the safety net**: enable
  `--gc-statepoints` on the gc-suite first (differential must stay byte-identical),
  then golden_ir + node, then test262, then flip the default.
- Fallback if cost is prohibitive: blanket-tenure movable object-literal/closure headers
  (roadmap Phase 0 option A) and document the constraint. Decide with measured numbers.
- **Exit:** `--gc-statepoints` default; nursery-on output byte-matches `TS_GC_NURSERY=0`
  across test262 + lodash + gc-suite; register promotion no longer pessimized by the
  conservative scan.

### 0.4 — Real weak references — **M**
WeakMap/WeakSet/WeakRef/FinalizationRegistry are currently fake-strong (deref always
returns target; finalizers are no-ops). Implement genuine weak semantics on top of the
(now precise) GC.
- **Exit:** weak-semantics compliance tests pass; a weakly-held-only object is collected.

### 0.5 — Permanent GC stress/fuzz lane in CI — **S**
Wire the gc-suite + a randomized allocation fuzzer into `tests/run_all.py` default lane
and a nightly stress lane: `TS_GC_STRESS=1`, `TS_GC_VERIFY=2`, differential vs
`TS_GC_NURSERY=0`. This is what keeps the gate closed (the rooting bugs fixed this
session would all have been caught deterministically by this lane).
- **Exit:** CI fails on any INV-1 violation, differential mismatch, or stress crash.

---

## Exit criteria for Phase 0 (the gate opens)
1. `--gc-statepoints` (or documented tenuring) is the default and the moving GC is
   correct-by-design.
2. nursery-on output byte-matches `TS_GC_NURSERY=0` across test262 + lodash + gc-suite.
3. Zero INV-1 violations under stress; no process aborts from a randomized GC fuzzer.
4. Weak references have real semantics and pass compliance.
5. The GC stress/fuzz lane is in CI and gating.
6. The async-work + C++-container rooting audits are complete and documented.

When these hold, proceed to roadmap Phase 1 (crash elimination + sound type dispatch).

## Key references
See GC-001 ticket's "Key references" for GC internals (`TsGC.cpp` minor collect/mark/
forward line numbers, alloc split, `TsGC.h` API). Statepoint codegen:
`src/compiler/codegen/CodeGenerator.cpp` (RS4GC, gc.relocate fixup, addrspace model,
FastISel). Harness: `tests/gc/runner.py`.
