# ts-aot → Production Grade: Roadmap

**Status:** Draft 2026-06-01 · **updated 2026-06-02**
**Target (chosen 2026-06-02):** **General Node replacement** — server + CLI, broadest
scope. Nothing is descoped by target.
**First focus (chosen 2026-06-02):** **GC correctness** (precise stack-map roots +
fuzzing). Actionable plan: **`docs/tickets/PHASE0-gc-memory-safety.md`**.
**Scope:** What it takes to move ts-aot from "impressive prototype" to a runtime you can
ship for real TypeScript/JavaScript workloads.

> **2026-06-02 progress on the Phase-0 gate** (narrows it, does not yet open it): the
> **libuv-struct-in-GC-memory rooting-gap class is closed** (11 sites — stream/socket/
> tty/childproc/http requests + fs watcher handles; `823dfabf`/`adc8b7a3`/`34f05859`);
> the **cross-generational write barrier is proven clean** (4 matrix programs `cb83b6b2`);
> the **moving-GC corruption remains unreproduced** and is most likely already fixed
> (lodash NURSERY=0 == default; harness 15/15 incl. cross-gen). Remaining gate work =
> finish the precise-roots path (`--gc-statepoints`, already substantially built in
> `CodeGenerator.cpp`), audit async-work + C++-container rooting, real weak refs, CI
> GC-fuzz lane. See the Phase-0 ticket.

> Methodology: this roadmap is synthesized from a five-axis codebase assessment
> (correctness, GC/memory-safety, performance, architecture/robustness, tooling/DX).
> Effort is sized **S / M / L** (days / weeks / month+), deliberately not false-precise.

---

## Verdict

ts-aot has production-grade **architecture** (AST→HIR→LLVM pipeline, type-specialization
passes, broad runtime) — typed numeric code already matches native C++. It is **not yet a
production-grade product**. Three categories block it, in priority order:

| Axis | State today | Production blocker |
|---|---|---|
| **GC / memory safety** | Custom generational *moving* GC with conservative stack scan; a **known, partially-unfixed register/container root-gap corruption** (see GC-001); weak refs are fake-strong; C++ container rooting unaudited | **YES — existential** |
| **Correctness** | ~53% test262 pass (excl. skips) vs. inflated "96%/99%" feature-matrix claims; long tail of error-semantics / iterator / descriptor / class gaps; ~450 runtime crashes | **YES** |
| **Robustness / arch** | 12k- and 10k-line monolith files; duplicated lowering paths; ~59 unvalidated `*(uint32_t*)magic` type-dispatch reads; fragile pointer heuristics | **YES (crash surface)** |
| **Performance** | typed code excellent; dynamic code slow (no inline caches, O(n) property lookup); optimization not enabled by default; conservative GC pessimizes regalloc; eager init + ICU bloat | Partial (caps competitiveness) |
| **Tooling / DX** | bare diagnostics (no `file:line:col`); native-only stack traces (no TS source mapping); no watch/incremental; no LSP; ICU runtime data dependency | YES (adoption) |

**The gate:** a moving GC with a register-only-root corruption can silently corrupt data
nondeterministically (this is the "heisenbug" class seen repeatedly). **No conformance,
performance, or tooling work matters until memory safety is closed.** Everything sequences
behind Phase 0.

---

## Phases

### Phase 0 — Memory safety (the gate) — **L**
Close the moving-GC corruption and make memory safety *provable in CI*. Extends the
in-progress **GC-001** ticket. Detailed plan: **`docs/tickets/PHASE0-gc-memory-safety.md`**.
- Drive one of: **(A)** tenure all movable types (pragmatic, lower-risk, throughput cost), or
  **(B)** finish precise GC statepoints so `--gc-statepoints` consumes roots at minor-GC and
  becomes default (correct-by-design, also unlocks register promotion).
- Audit every C++ container holding GC pointers (scanner + minor-fixup).
- Implement real weak references (WeakMap/WeakSet/WeakRef/FinalizationRegistry).
- Permanent **GC stress/fuzz lane in CI** (`TS_GC_STRESS`, `TS_GC_VERIFY=2`, differential vs `TS_GC_NURSERY=0`).
- **Exit:** nursery-on output byte-matches `TS_GC_NURSERY=0` across test262 + lodash + gc-suite; zero INV-1 violations under stress; weak semantics pass their compliance tests.

### Phase 1 — Crash elimination + sound type dispatch — **M/L**
The ~450 test262 crashes + the heisenbugs share a root: **wrong-typed pointer dereference**
via unvalidated magic reads (~59 sites) and fragile heuristics (`ts_is_closure` tag,
closure-cell def-chain walk, flat-object `@@iterator` gap). Production = the process never
crashes on bad input.
- Replace ad-hoc `*(uint32_t*)ptr` dispatch with a **validated type-tag accessor** (range-check + single tag read).
- Harden the exception/unwind model (ensure throws propagate, not swallowed/aborted).
- Fix `ts_iterator_get` for flat-object `@@iterator`.
- **Exit:** test262 `crash` count → ~0; fuzz/stress finds no process aborts.

### Phase 2 — Correctness conformance (53% → ~80%) — **L (multi-cycle grind)**
A systematic grind, not one lever (confirmed empirically). ROI order:
1. **Error semantics** — `RequireObjectCoercible`/receiver validation across built-ins (~1,400 "expected TypeError, none thrown").
2. **Iterator-close protocol** — `try/finally → iterator.return()` on break/throw in for-of + destructuring (~650 + crashes; extends the array-destructuring-via-iterator fix already landed `9f7fd221`).
3. **Property-descriptor validation** state machine for `Object.defineProperty` (~800).
4. **Class semantics** audit (~3,000, fine-grained — steady chipping).
5. **Parser early-error pass** — dedicated post-parse validation for ~525 "should-be-SyntaxError" (defer the ICU-divergent RegExp subset).
- **Also:** replace the feature-existence conformance matrices with **test262-pass-rate per area** so progress is honest.
- **Exit:** ~80% test262 (of the non-impossible subset); eval/realms/Temporal explicitly scoped out.

### Phase 3 — Tooling / DX — **M** (parallel with Phase 2)
Required for anyone to adopt and debug it.
- Diagnostics with `file:line:col` + error codes (AST already carries locations — wire them through `reportError`).
- **TypeScript source mapping in runtime stack traces** (mangled-name → TS file:line) so `Error.stack` and crash dumps point at user code.
- Self-contained binaries by default (bundle ICU); then watch/incremental builds; then LSP.

### Phase 4 — Performance — **M/L** (last; one early near-free win)
The reason to AOT-compile; only matters once correct + safe.
- **Enable optimization by default** (near-free once Phase 0 settles the GC-rooting/regalloc interaction).
- **Inline caches + shape-based property access** for dynamic code (biggest dynamic win, ~2–5×).
- Lazy module init + ICU-footprint reduction (startup/size).
- Stand up the **benchmark suite in CI** (exists, no published numbers → perf regressions invisible today).

### Cross-cutting (continuous)
- Refactor duplicated lowering paths (two class paths, two name-emission blocks) into shared helpers.
- Incrementally de-monolith `ASTToHIR.cpp` / `HIRToLLVM.cpp` (12k/10k lines).
- Clean, non-noisy test262 baseline + CI gates on golden_ir / node / test262 / lodash / gc.
- Delete/replace stale docs (`docs/gc-roadmap.md` still describes the removed Boehm GC).

---

## Sequencing & the shortest honest path

```
Phase 0 (gate) ──► Phase 1 ──► Phase 2 ──┐
                              Phase 3 ────┼──► Phase 4
                       (2 & 3 in parallel)┘
```

**Shortest line to "shippable for real workloads":**
Phase 0 + Phase 1 + Phase 2 items 1–2 (error semantics, iterator-close) + Phase 3 a–c.
That is the boundary between *prototype* and *production-grade*. Phase 4 and the long
conformance tail are quality/competitiveness, not the gate.
