# GC-001 — Comprehensive GC Verification Harness + Fix the Moving-GC Corruption

**Status:** IN PROGRESS (Phase 1 + Phase 3a landed; Phase 2 + Phase 3b remain)
**Created:** 2026-05-27
**Owner:** (GC initiative)
**Branch:** `gc-001-verification-harness`

---

## Progress log

### 2026-05-27 — Phase 1 (harness) + Phase 3a (object-literal tenuring) landed

**Phase 1 — verification harness core (DONE, commits c5ecec4 / f680ec8 / ccdcd97):**
- **1a** TS-callable builtins wired through the analyzer + ASTToHIR (handled at
  the TOP of the bare-identifier call path so the analyzer's FunctionType
  registration can't divert them to a weak undefined stub) + HIRToLLVM generic-
  call ABI (double/bool/void returns): `__ts_gc_minor`, `__ts_gc_major`,
  `__ts_gc_is_nursery`, `__ts_gc_collection_count`, `__ts_gc_live_size`,
  `__ts_gc_verify`. Runtime: `ts_gc_dbg_*` + `ts_gc_verify_now()`.
- **1b** `TS_GC_VERIFY=N` levels (1=report, 2=abort-on-INV-1-violation,
  3=deep Part B/C). INV-1 (the existing Phase 5v post-forwarding scan) now
  ABORTS under N>=2. Heavy Part B (scanner re-run) + Part C (full-process
  VirtualQuery, faults on guard pages) gated behind `g_verify_forward_deep`
  so the on-demand verify runs only the safe old-gen+roots+pinned scan.
- **1c** `tests/gc/` suite: runner.py runs each program under default /
  TS_GC_NURSERY=0 / TS_GC_VERIFY=2 (+`--stress`), enforcing a PASS contract,
  a **differential** (moving-GC stdout must byte-match the NURSERY=0 baseline)
  and INV-1-no-abort. 6 programs spanning object-type × holder × trigger.
  Registered as the `gc` suite in `tests/run_all.py`. All green.

**Phase 3a — the fix (DONE, commit 6aee00d):** tenure escaping object-literal /
flat-object headers to old-gen (`ts_flat_object_create`, and the
`lowerNewFlatObject` heap path's inlined `__ts_nursery_alloc` → `ts_gc_alloc_old_gen`).
Non-escaping objects still stack/SROA; shapeless `{}` (`TsMap::Create`) left in
nursery. Validated: gc-suite differential-clean, golden_ir 266/278 + node 295/297
(no regressions), test262 object clusters clean (3 built-ins/Object "regressions"
are pre-existing stale-baseline artifacts on the shapeless `{}` path, unrelated).

**Key finding — minimal repro is insufficient & lodash has a separate blocker:**
- The object-literal corruption does NOT reproduce in a minimal single-forced-GC
  program (matches prior sessions). The reliable detectors are the DIFFERENTIAL
  check + INV-1 abort + the lodash harness.
- The lodash upstream harness currently hits an **early init failure** that is
  NOT the moving-GC bug: it reproduces with `TS_GC_NURSERY=0` AND on the pre-3a
  build (pre-existing, not a 3a regression). With `TS_GC_NURSERY=0` it terminates
  via a C++/JS exception (`0xe06d7363`) during init — i.e. an uncaught throw /
  logic bug, NOT memory corruption — so it belongs to the lodash effort
  ([[lodash-upstream-testjs-harness]]), not the moving-GC fix. It blocks
  end-to-end lodash assertion-density measurement until fixed. Under nursery-ON
  the failure instead surfaces a corrupted CLSR magic (an access violation),
  implying OTHER movable types (TsArray element buffers, TsString headers,
  TsBigInt, shapeless TsMap) still corrupt under the moving nursery — i.e.
  per-type tenuring is whack-a-mole and Phase 3b (precise minor-GC roots) is the
  real general cure. The gc-suite differential programs for arrays/strings/sets
  pass at their current scale, so the surviving corruption needs full-harness
  scale to manifest (consistent with the object-literal case).

**Phase 3b migration progress (2026-05-27):**
- STEP 1 DONE (commit on branch): replaced all 639 `builder_->getPtrTy()` in
  HIRToLLVM with `getGCPtrTy()` (addrspace(1) under --gc-statepoints, else
  addrspace(0)). Default-safe by construction (byte-identical IR with statepoints
  off; golden_ir/gc-suite green). Fixed `gcPtrToRaw` to target addrspace(0)
  explicitly. Under the flag, GC pointers now flow addrspace(1) through the ~322
  ad-hoc call sites.
- NEXT SUB-PROBLEM (precisely characterized): the blanket migration is too coarse.
  The statepoints-on module now fails the verifier with "Call parameter type does
  not match function signature" in two classes:
    (a) RAW-pointer params wrongly made addrspace(1): e.g. `ts_string_create(const
        char*)` — its arg is a global string-literal constant `@0` (addrspace 0),
        but the param is now addrspace(1). Raw `char*`/buffer params (and the
        string/data GLOBALS feeding them) must EITHER stay addrspace(0) (needs a
        per-param GC mask: which runtime params are GC ptrs vs raw) OR the globals
        must also be emitted addrspace(1) for a fully-uniform model.
    (b) Residual laundering: `ts_value_make_string(%gc.to.raw)` passes an
        addrspace(0) laundered arg to an addrspace(1) param — remove the
        gcPtrToRaw laundering on GC args (keep them addrspace(1)).
  DECISION MADE (2026-05-27, 4 research agents incl. Julia/RS4GC precedent):
  **per-VALUE classification with an addrspace(0) C-ABI boundary; NO per-param table.**
  Decisive RS4GC fact (LLVM statepoint docs): RS4GC relocates an addrspace(1) value
  iff it is LIVE ACROSS the call — it does NOT require call ARGUMENTS to be
  addrspace(1). Correct design (OPPOSITE direction from step 1's signature change):
    • GC-managed VALUES inside compiled code = addrspace(1) (step 1 already does this);
    • runtime fn SIGNATURES (pointer params/returns) = addrspace(0) (real C ABI),
      via ONE canonical declarer so getOrInsertFunction's one-signature-per-name cache
      stays consistent — NO per-param GC table (the boundary is uniformly addrspace 0,
      dissolving the GC-vs-raw-char* ambiguity that would otherwise need a table);
    • at each call: addrspacecast GC pointer ARGS (1)->(0) sunk right before the call,
      KEEP the addrspace(1) originals live across it (so they enter gc-live/gc.relocate),
      cast pointer RETURNS (0)->(1);
    • function pointers, globals, vtables, constants stay addrspace(0) (not roots).
  REJECTED: uniform-addrspace(1)-incl-globals (function-ptr/global relocation risk,
  not what real frontends do); per-function GC-mask table (unnecessary given the
  addrspace-0 boundary).
  DEEP TENSION (flag for later): RS4GC ideally wants addrspace(1) NON-INTEGRAL (`ni:1`)
  so the optimizer can't fold int<->ptr and drop liveness — but non-integral FORBIDS
  ptrtoint/inttoptr, which ts-aot NaN-boxing uses on object refs (48 sites). So `ni:1`
  is incompatible with current NaN-box codegen without Julia's intrinsic-confined
  int<->ptr + custom late-lowering pass (weeks). NEAR-TERM: integral addrspace(1)
  (no ni:1), validate roots empirically (harness differential + count); escalate to
  Julia-style only if liveness proves fragile.
  STEP 2 DONE (commit cf4d06e): implemented as a CENTRALIZED IR pass
  `normalizeRuntimeBoundaryAddrSpaces` (CodeGenerator.cpp), run at the top of
  emitObjectFile under --gc-statepoints only (default build untouched), NOT by
  editing 322 sites. Phase 1 rewrites external runtime DECLARATIONS to addrspace(0)
  pointer params/return + rebuilds call sites with arg (1)->(0)/result (0)->(1)
  casts; Phase 2 reconciles arg addrspaces at ALL external-decl calls (incl. those
  already addrspace(0) via handlers). Original addrspace(1) values stay live →
  RS4GC relocates them. MILESTONE HIT: root count 0→N — trivial 0→18,
  array_of_objlits 0→60; 7/9 tests/gc programs (objects/arrays/strings/maps/sets/
  nested) verify + 38-102 roots + run correctly under --gc-statepoints. Default
  build green (golden_ir 266/278, node 295/297).
  STEP 3 PROGRESS (2026-05-27):
  • 3a DONE (commit): closures/nested-fn GC returns were lost under statepoints
    (OBJ-LOST/CLO-LOST) because HIRToLLVM gcPtrToRaw laundered GC values to
    addrspace(0) and stored them into locals (invisible to RS4GC). Made gcPtrToRaw
    a no-op (boundary pass handles casts). ALL 9 tests/gc programs now PASS under
    --gc-statepoints with roots recorded (44-236). Default build green.
  • 3b WIRED but INEFFECTIVE (honest finding): added ts_gc_push_precise_stack_roots()
    to the minor GC mark phase. BUT the precise stack-walk (push_precise_roots_win64,
    GCRoots.cpp) reports "0 safepoints, 0 roots" at GC time for EVERY collection —
    even programs whose stack map has 120 safepoints / 86 roots. So the GC-time
    return addresses (RtlVirtualUnwind ctx.Rip) never match the stack-map keys
    (`functions[].address + instrOffset + imageBase`). This is a PRE-EXISTING bug
    affecting BOTH full and minor precise rooting — precise rooting has never
    actually found roots from the GC-triggered context. Likely return-address-vs-
    statepoint-address (call-length) or image-base/ASLR relocation mismatch in the
    parse/lookup. NEXT: instrument ctx.Rip vs nearest stackmap key, fix the
    address arithmetic so lookup matches, then precise rooting becomes effective.
    UPDATE (same day, deeper dig): the stack-map address computation in
    StackMap.cpp WAS wrong — it added imageBase to `functions[].address` (already
    loader-relocated absolute), producing invalid 0xFFEC.. keys (>48-bit) → every
    lookup missed → 0 roots (a SAFE no-op). Removing the imageBase addition moved
    the keys into valid image space, BUT (a) the safepoint offsets still don't
    align with where user code executes (range 0x101C-0x1770 vs user frames at
    +0x31A63) AND (b) enabling root extraction with the now-valid-ish addresses
    made the STILL-BUGGY extract_root_pointer read wrong slots and CORRUPT 2 of 9
    gc programs (9/9 -> 7/9). So precise rooting is doubly broken (address offsets
    + root extraction) and was REVERTED to the safe 0-roots no-op. CONCLUSION:
    precise stack-map rooting does NOT work and is NOT what fixes the corruption.
    The moving-GC corruption fix under statepoints comes ENTIRELY from RS4GC
    SPILLING live GC pointers to stack slots at safepoints + the existing
    conservative minor-GC scan + Phase 7 (which catch the spilled pointers) — this
    is a legitimate, sufficient fix (no register-only-resident live pointer can
    exist across a safepoint once RS4GC spills). 9/9 gc programs PASS under
    --gc-statepoints. The 3b precise-consumption call is wired (committed) but
    no-ops until the address-offset + extraction bugs are fixed (a separate,
    multi-step PE/COFF stack-map debugging task — NOT required for correctness).
    CAVEAT unchanged: the corruption fix is UNPROVEN (no minimal repro fails;
    needs the lodash differential once lodash's separate early-init crash is fixed).
  • CORRECTNESS CAVEAT: the 9/9 statepoints PASS comes from RS4GC SPILLING live GC
    pointers to stack slots at safepoints + the existing conservative minor scan +
    Phase 7 — NOT from precise stack-map consumption (which no-ops). AND the
    moving-GC corruption fix remains UNPROVEN under statepoints because no minimal
    repro reproduces the corruption (the gc programs pass conservatively too); the
    real proof needs the 0-safepoints fix + a differential on a corruption-prone
    workload (lodash, once its separate early-init crash is resolved).

  (original step-3 follow-ups, now superseded by the above:)
  • (a) closure_cells_survive / closure_queue produce roots (168/188)
  but CORRUPT closures under statepoints — function-pointer / closure-cell dispatch
  interaction with the addrspace migration (Agent C flagged function-ptr addrspace
  risk); (b) make the MINOR GC consume the precise stack-map roots (today
  ts_gc_push_precise_stack_roots feeds only the full GC — TsGC.cpp:1078); then
  validate the gc-suite differential under statepoints, drive out golden_ir/node/
  test262 regressions, and measure lodash. Default build stays the green invariant.

**Remaining:**
- Phase 2: the full parameterized type×holder×trigger matrix + shadow-heap
  fuzzer + revived Catch2 white-box tests.
- Phase 3b: precise minor-GC stack-map roots (the general cure for ALL movable
  types) — high risk, do incrementally with the Phase-1 harness as safety net.
  FEASIBILITY (scanned 2026-05-27): the statepoint path already EXISTS but is
  default-OFF behind `--gc-statepoints` (`CodeGenerator.h` `enableGCStatepoints_`,
  `Driver.h`): when enabled, HIRToLLVM emits GC pointers in `addrspace(1)`
  (HIRToLLVM.cpp:40 + rawToGCPtr/gcToRawPtr at :101/:107), CodeGenerator runs
  `RewriteStatepointsForGC` + adds deopt bundles + fixes LLVM-18.1 gc.relocate
  off-by-one (CodeGenerator.cpp:187/337-363), and `TsAotGC` (`UseRS4GC=true`) is
  the strategy. So Phase 3b == turn this on by default and make it robust
  (addrspace-1 correctness across all opcodes, RS4GC over the whole module,
  minor GC consuming the stack maps via `ts_gc_push_precise_stack_roots`), then
  drive out regressions with the gc-suite + golden_ir + node + test262. It is a
  whole-codegen flag-flip-and-fix — start it only when it can be finished/reverted
  cleanly in-session.
  DIAGNOSIS (2026-05-27, why `--gc-statepoints` roots nothing today): with the
  flag on, the build compiles and runs, RS4GC runs over the deopt-bundled calls
  and the module verifies — BUT `[StackMap] Parsed N safepoints with 0 GC root
  locations` and NO "Fixed N gc.relocate indices" → **RS4GC produces ZERO
  gc.relocates**, so precise rooting is a complete no-op. Root cause: RS4GC only
  relocates `addrspace(1)` SSA values that are LIVE ACROSS a statepoint, but
  HIRToLLVM's `createRuntimeCall` (and the codegen generally) **launders every GC
  pointer back to raw `addrspace(0)` via `gcPtrToRaw` before each call** and
  re-derives from the return — so no addrspace(1) value ever spans a call. The
  GC pointers also live in `gc.pin` allocas (mem2reg promotes them at O2, but the
  addrspace(1) SSA value's only use is the immediate cast-to-raw, so it is dead
  before the call). Compounding: `createRuntimeCall` is used at ONLY 1 site;
  ~322 call sites build their own FunctionType with `getPtrTy()` (addrspace 0)
  and pass laundered raw args; and the DataLayout is the target default (addrspace
  1 NOT marked non-integral `ni:1`).
  → Making statepoints actually root is therefore a PERVASIVE runtime-call-ABI
  migration, not a flag flip: (1) DataLayout: mark addrspace-1 non-integral when
  statepoints on; (2) a CENTRAL runtime-fn declaration+call helper that, under
  statepoints, declares GC-pointer params as addrspace(1) and passes addrspace(1)
  args (the C ABI is identical — addrspace is IR-only) WITHOUT laundering, so GC
  values stay addrspace(1) live-across-statepoint; (3) migrate all ~322 call
  sites to it (the verifier requires one consistent signature per runtime fn, so
  this must be all-or-nothing per function); (4) make the MINOR GC consume the
  stack-map roots (`ts_gc_push_precise_stack_roots`, currently full-GC-only).
  Validate incrementally with the gc-suite differential + INV-1 + golden_ir/node/
  test262. This is multi-session/large; the bounded alternative that is already
  WORKING is to keep tenuring movable types (Phase 3a-style) under the harness.
  NaN-box note (CORRECTED 2026-05-27): an earlier note here claimed NaN-boxing
  makes object pointers invisible to RS4GC. That was WRONG. `nanbox_ptr(p)`
  (TsNanBox.h:64) returns the raw pointer unchanged — ts-aot's pointer tag is
  "top 16 bits = 0", so a NaN-boxed object pointer is BIT-IDENTICAL to its raw
  address (passes `is_nursery_ptr`) and flows as a pointer-typed value (RS4GC can
  track it if kept addrspace(1)). The actual corruption (BUG 4) is a FORWARDING
  gap for **register-resident holders** — proven because `TS_GC_PROMOTE_ALL`
  (mark everything) does NOT fix it (so it's not under-marking) — a pointer live
  only in a callee-saved register at the GC instant is scanned by neither the
  conservative stack walk nor Phase 7, so after the object moves the register
  still holds the old (soon-zeroed) address. Statepoints/RS4GC ARE the right
  engine-grade fix for this (they enumerate live GC pointers incl. register
  locations at each safepoint — the same thing V8/SpiderMonkey/JSC safepoint
  tables do). They root nothing TODAY only because of the addrspace(0) laundering
  at ~322 call sites, NOT because of NaN-boxing. The one real NaN-box wrinkle is
  narrow: a value `ptrtoint`'d to i64 and held across a safepoint as an integer is
  untracked (so the tag-check/boxing helpers must avoid holding the integer form
  across calls); an object ref held as a pointer-typed value is fine.
- The separate lodash early-init crash (NURSERY=0-reproducible) — needs its own
  investigation before lodash can quantify the fix.

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

### 2026-06-02 — TS_GC_STRESS stream crash diagnosed: it is NOT the moving-GC corruption

**Correction of a prior assumption.** The "4 stream node tests flip pass→fail under
TS_GC_STRESS=1" repro (flagged earlier as a fresh moving-GC repro) is **NOT** the
moving-GC corruption GC-001 Phase 3b targets:
- `TS_GC_STRESS` itself DISABLES the nursery (`nursery_disabled = (g_gc_stress>0)`,
  TsGC.cpp ~1606). So "stress" and "stress + TS_GC_NURSERY=0" are the SAME config
  (nursery off) — the differential I ran was meaningless; I never exercised the
  moving nursery. The moving-GC corruption needs nursery ON (full-harness scale,
  per prior notes), still unreproduced minimally.

**What the stream-stress crash actually is (root-caused via /auto-debug):**
- Crash: `0xc0000005` write-AV in libuv `uv__queue_insert_tail` on a **thread-pool
  WORKER thread** (`worker` → `uv__thread_start`), `mov [rax],rdi` with
  rax=0x50040138, rdi=0x50080138 — TWO ADJACENT GC BLOCKS (BLOCK_SIZE=256KB=0x40000,
  the exact delta; GC region ~0x5000_0000). `platform_free_block` VirtualFrees blocks
  → unmapped → AV.
- Mechanism (confirmed): async libuv work structs are malloc'd but hold UNROOTED GC
  pointers. e.g. `WriteFileWork`/`FSFdAsyncWork` (fs.cpp) are `malloc`'d and set
  `work->promise = ts_promise_create()` (GC) with NO `ts_gc_register_root`. Between
  `uv_queue_work` dispatch and the after-work callback, the promise (and other GC
  objects only reachable through the malloc'd work) is invisible to the GC. The
  `g_gc_stress` trigger (TsGC.cpp ~700) fires a FULL collect on EVERY ts_alloc with
  **no thread guard** → collects/VirtualFrees the in-flight GC object → the libuv
  worker thread (running concurrently, GC does NOT pause workers) faults on freed
  block memory. This is the runtime-safety.md "malloc'd container holding a GC
  pointer is invisible to GC" gap, in the async-I/O paths.
- So it's a REAL latent rooting gap (can fire rarely in production when GC hits
  during async I/O), amplified to determinism by TS_GC_STRESS — but it is a SEPARATE
  bug from the moving-GC corruption.

**Fix direction (not yet implemented):** root the GC pointers held by in-flight async
work structs for the operation lifetime — `ts_gc_register_root(&work->promise)` (and
any callback/buffer GC ptrs) on dispatch, unregister in the after-work callback;
OR store them in a GC-scanned registry; OR a shared "pending async roots" set. Applies
to ALL `uv_queue_work` sites (fs writeFile/fd-async/exists/read/..., crypto pbkdf2/
scrypt/keygen). Secondary hardening: thread-guard the GC so a worker-thread ts_alloc
never triggers collection, and/or pause/again-coordinate libuv workers across GC.

**Status:** diagnosis complete + recorded; fix (root in-flight async GC ptrs) is the
next implementation step. The moving-GC corruption (the original Phase 3b target)
still needs a nursery-ON repro.
