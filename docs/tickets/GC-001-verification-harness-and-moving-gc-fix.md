# GC-001 — Comprehensive GC Verification Harness + Fix the Moving-GC Corruption

**Status:** OPEN (goal prompt — drives a multi-session effort)
**Created:** 2026-05-27
**Owner:** (GC initiative)

---

## GOAL PROMPT (paste this to start a working session)

You are building a **comprehensive test/verification harness for the ts-aot garbage collector and then fixing a confirmed moving-GC correctness bug**, with the harness as the proof-of-correctness and the regression safety net. Work in phases; each phase ends with a clean `cmake --build build --config Release`, `python tests/run_all.py` (golden_ir + node, 0 regressions), and a commit. Per project memory: **runtime edits require a FULL build** (not `--target ts-aot`); **never build during a test262 sweep**; use the **/auto-debug** skill (never raw cdb) for crashes.

### Confirmed root cause (do not re-derive — verify, then fix)
ts-aot's custom generational GC (`src/runtime/src/TsGC.cpp`) runs **minor (nursery) GC with conservative-only stack rooting**. `enableGCStatepoints_` is **false by default** (`src/compiler/hir/HIRToLLVM.h:105`, `src/compiler/codegen/CodeGenerator.h:24`; only `--gc-statepoints` flips it) so **no LLVM stack maps are emitted**, and `ts_gc_push_precise_stack_roots` (`src/runtime/src/GCRoots.cpp`) is invoked **only by the full GC** (`TsGC.cpp:1078`), never the minor GC. Minor GC therefore relies on `gc_pin_nursery_stack_roots` (`TsGC.cpp:~2101`) + a Phase-7 exact-match stack rewrite (`TsGC.cpp:~2874`). A live pointer held only in a caller-saved register / spill slot at the GC instant is **not pinned**, so a **movable** nursery object it solely references is promoted (moved), the holder isn't forwarded, and the evacuated nursery slot is zeroed on reset → reads back magic `0` / all-undefined properties.

Proof already obtained: with `TS_GC_NURSERY=0` the corruption vanishes; with `TS_GC_PROMOTE_ALL` (everything promoted but still moving) it persists. Closures/cells were fixed by **tenuring** to old-gen (`ts_gc_alloc_old_gen` in `TsClosure.cpp`/`TsCell.cpp`, commit `5f4501e`). Object literals (flat objects via `ts_flat_object_create` `TsFlatObject.cpp:61` / inlined `__ts_nursery_alloc` `HIRToLLVM.cpp:~4047`; and TsMap-backed via `ts_map_create`→`TsMap::Create` `TsMap.cpp:65`) are still nursery-allocated → still corrupt. Movable types: `TsObject`, flat objects, `TsArray`+element buffer, `TsMap`/`TsSet` headers, `TsString` header (default `Create`), `TsBigInt`, `TsSymbol` (one path). Tenured already: closures, cells, cells-array, `TsHashTable`+buffer, `TsRegExp`, interned/old-gen strings & symbols.

### Phase 1 — Verification harness core (PROVE it, make the bug deterministic). Highest value.
1. Add force-GC + introspection **builtins** callable from compiled TS (wire in the compiler's builtin/extension registry; runtime fns mostly already exist in `TsGC.h`): `__ts_gc_minor()` → `ts_gc_minor_collect`; `__ts_gc_major()` → `ts_gc_force_collect`; `__ts_gc_is_nursery(obj)`; `__ts_gc_collection_count()`; `__ts_gc_live_size()`; `__ts_gc_verify()` → new `ts_gc_verify_now()` returning a failure count. These make a single alloc + single forced GC reproduce what currently needs 4 MB of churn.
2. Implement **asserting verification invariants** in the collector, gated by a single `TS_GC_VERIFY=<1|2|3>` (keep existing named flags as aliases). Factor the existing `VERIFY_CARDS`/`VERIFY_FORWARD` scan loops into reusable `gc_verify_*` helpers; turn **report → assert (abort with holder+target magic, like Go `gccheckmark`)**:
   - **INV-1 (most important): no-stale-pointer after minor GC.** After all fixup phases, before nursery reset (`TsGC.cpp:~2580`), assert no reachable slot (old-gen blocks + large objects + global roots + scanners + pinned-survivor internals; level 3 = full committed-process VirtualQuery scan already prototyped) points into the evacuated nursery. Classify STALE (promoted, holder not forwarded) vs DANGLING (target deemed dead but still referenced).
   - **INV-3: mark completeness (dual-liveness).** Re-derive nursery reachability with a second independent pass; assert equal to `gc_mark_nursery_live`. (Precise version of `TS_GC_PROMOTE_ALL`.)
   - **INV-5: magic/shape validation** on a full live-object walk (valid magics: CLSR 0x434C5352, CELL 0x43454C4C, MAPS 0x4D415053, ARRY, STRG, SYMB 0x53594D42, FLAT 0x464C4154, …); a `0`/garbage magic on an allocated slot is the corruption signature. Maintain one magic registry.
   - **INV-6: poison evacuated nursery + swept old-gen** with `0xDE` (under the flag) so use-after-promote traps immediately at a recognizable value.
   - (Follow-ons: INV-2 barrier/remembered-set completeness, INV-4 forwarding completeness/uniqueness, INV-7 pin-set integrity, INV-8 heap accounting.)
3. **Deterministic repro test:** a TS program that allocates one object literal `{a,b,fn}`, holds the only ref in a way that triggers the bug, calls `__ts_gc_minor()`, asserts integrity. It MUST fail on current master (proving the harness catches the real bug) and pass after Phase 3.

### Phase 2 — The parameterized test matrix (PROVE it broadly).
Build a generated suite over **object-type × holder-kind × GC-trigger**:
- **Types:** TsCell, TsClosure(+cell-array), plain object (inline-slot + map-backed), array (small + large), TsString, TsMap/TsSet entries, TsSymbol, TsBigInt, boxed value, Promise, Date, typed-array/Buffer backing, Proxy target.
- **Holders:** stack local; closure cell; array element; object inline field; object dynamic (map) property; Map value / Set member (old-gen bucket → Phase-3 full-scan path); module/global root; deep field chain `a.b.c.d.e`; scanner-reported edge.
- **Triggers:** forced minor; forced major; natural nursery-overflow; `TS_GC_STRESS=1`; `TS_GC_NURSERY=0` (differential baseline — output must be byte-identical to the moving run).
- **Categories:** single-holder survival (core); promotion correctness; deep-graph; wide-graph; cyclic refs; **old→young write-barrier** regression; many-small-objects stress; large-object/size-class-boundary; **seeded shadow-heap fuzzer** (logged seed → replayable minimal repro); **differential** (moving vs `NURSERY=0`).
- **Delivery:** primary = TS programs compiled by ts-aot under `tests/gc/` with a `runner.py` cloned from `tests/invariants/runner.py` (PASS/FAIL contract, hashed exe names); register a `gc` suite in `tests/run_all.py`. Secondary = revive `tests/unit/` Catch2 against the real `TsGC.h` API, CMake-wire a `ts_runtime_tests` target + CTest, for white-box invariant unit tests. Run each test in configs {default, `TS_GC_STRESS=1`(curated subset), `TS_GC_NURSERY=0`, `TS_GC_VERIFY=2`}; a test must pass in all.

### Phase 3 — Fix the moving-GC corruption (validated by the harness).
- **Step 3a (pragmatic, unblocks now):** tenure object-literal headers to old-gen — `TsMap::Create` (`TsMap.cpp:65`), `ts_flat_object_create` (`TsFlatObject.cpp:61`), and the `lowerNewFlatObject` heap path (`HIRToLLVM.cpp:~4047`) → use the old-gen allocator. Validate: harness goes green with the nursery ENABLED; 0 golden_ir/node regressions; **benchmark GC throughput / old-gen pressure** (object literals are far more numerous than closures — this is the real risk); confirm test262 net-neutral (per-test A/B vs parent commit, watch for flaky Map-iteration/timeout noise).
- **Step 3b (the real general cure):** make minor GC use **precise stack-map roots** (enable statepoints / RS4GC by default, or push precise roots into the minor-GC root set). This eliminates the defect class for ALL movable types and is the only way to truly "prove the moving GC correct." High risk (addrspace flip, whole-ABI RS4GC, test262 regression potential) — do it **with the Phase-1/2 harness as the safety net**, incrementally, measuring at each step. If 3b proves too costly, the harness lets us decide to keep tenuring + document the constraint.

### Success criteria
- The harness reproduces the object-literal corruption **deterministically** (single forced GC, no scale) on the unfixed build, and goes green after the fix **with the nursery enabled**.
- Full type×holder×trigger matrix passes in all GC configs; differential (moving vs `NURSERY=0`) byte-identical.
- `python tests/run_all.py` clean; test262 net-neutral.
- The GC suite is wired into `run_all.py` (default lane) + a nightly stress/fuzzer lane.
- lodash `test/test.js` assertion density returns to ~normal with the nursery ON (the original symptom), and we can finally quote a real lodash pass-rate.

### Key references
GC: `src/runtime/src/TsGC.cpp` (minor GC `gc_minor_collect_internal` ~2188; mark `gc_mark_nursery_live` ~1835; pin ~2101; forwarding `lookup_forward` ~2286; existing VERIFY_FORWARD ~2581; Phase-7 stack rewrite ~2874). Roots: `GCRoots.cpp`. Alloc split: `ts_gc_alloc` ~1508 vs `ts_gc_alloc_old_gen` ~1625; `NURSERY_MAX_OBJ_SIZE`=256. Object creation: `HIRToLLVM::lowerNewObject` ~3910 / `lowerNewFlatObject` ~3985 / nursery alloc fn ~10175; `TsMap::Create` `TsMap.cpp:65`; `ts_flat_object_create` `TsFlatObject.cpp:61`. API: `src/runtime/include/TsGC.h`. Runners: `tests/run_all.py`, `tests/invariants/runner.py`, `tests/golden_ir/runner.py`. Stale C++ tests: `tests/unit/RuntimeTests.cpp`.

### Notes
- Sequencing rationale: harness FIRST so the fix is provable and regressions are caught — this exact bug class (BUG 4/6/7) would have been caught deterministically by INV-1 + a single-holder test.
- Stop adding runtime null/off-by-8 guards (they mask, not fix). The off-by-8 corrector (`TsMap::self()`, commit `10c27d4`) and read-path guards (`a515a03`) are symptoms; the harness + tenuring/precise-roots is the cure.
