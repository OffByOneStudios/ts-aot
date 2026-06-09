# ts-aot Architecture & Test-Bucketing Review

Executed 2026-06-09 against the sweep in `tests/test262/.test262_results.jsonl`
(21,847 pass / 12,836 fail / 11 compile_error / 56 timeout / 15,756 skip —
verified exactly). Five parallel code/data audits: object model, array
subsystem, GC, compiler private-method lowering, and a quantitative methodology
audit (scripts left at `tmp/audit262.py`, `tmp/audit262b.py`).

---

## A. Architecture review

### A1. The object model is coherent — and the prototype chain is *half built*, which changes the cost calculus

The framing in the prompt ("plain objects have NO [[Prototype]] link") is
slightly too pessimistic, and that matters for the lever ranking:

- `TsMap` **already has** a real prototype pointer (`TsMap.h:91`,
  `GetPrototype`/`SetPrototype`), and `ts_object_get_property` **already walks
  it** in a loop (`TsObject.cpp:4372-4399`), as does the `in` operator for
  TsMap receivers (`TsObject.cpp:11122-11127`) and for-in enumeration via a
  `getPrototypeOf` loop with a `seen` dedup map (`TsObject.cpp:6610-6644`).
  `Object.create(null)` is modeled (`nullPrototype` flag, `TsMap.h:96`);
  `setPrototypeOf` has cycle detection (`TsObject.cpp:7101`).
- What's missing is the **chain terminal**: when the walk reaches null, ~8
  distinct special-case sites supply hardcoded builtin methods
  (`TsObject.cpp:3951-3987` flat objects, `4408-4434` TsMap, `4515-4520`
  arrays, `3894-3928` number/bool primitives, `9970-10053` strings,
  `10382-10388` native objects, plus `is_object_prototype_member()` at
  `11025-11054` for `in`, and the native implementations at `12184-12591`).
  Rough count: **150-200 branches** across receiver types.
- Flat objects and TsArray do **not** walk any chain at all on the get/has
  paths; their fallbacks go straight to the hardcoded branches.

**Verdict:** the missing real `Object.prototype` is a load-bearing crack, but
the fix is *not* a rewrite — it is "give the existing walk a real terminal."
A GC-rooted singleton `TsMap* g_object_prototype` (init in `TsGlobals.cpp`,
`ts_gc_register_root`), populated with the existing native builtins as
**non-enumerable** entries (`__attrs_` bits already exist for this), consulted
when a chain walk reaches null on a non-`nullPrototype` object. The hardcoded
branches can stay temporarily as a last-resort fallback — they become
semantically equivalent to a chain miss, so they can be deleted incrementally
rather than atomically. Lookup order stays spec-correct because the TsMap walk
already runs *before* the hardcoded fallback.

Blast radius, quantified:
1. **Enumeration leakage is the #1 risk.** for-in already walks real
   prototypes; the moment `getPrototypeOf({})` returns a populated singleton,
   every builtin must carry `enumerable=false` attrs or for-in over `{}`
   enumerates `hasOwnProperty`. This is detectable cheaply (a 5-line tmp probe
   + the lodash suite, which is enumeration-heavy).
2. **Flat objects and arrays need the walk added** (they have none today) —
   two functions each (`ts_object_get_property`, `ts_object_has_prop`), low
   line count, hot path. Gate behind "own lookup missed" so the fast path is
   untouched for hits.
3. **Perf cliff** is bounded: plain objects get at most a one-level walk to the
   singleton on *misses only*; deep chains only exist where users built them.

Direct test262 yield is the ~36-test deleted-prototype-index cluster plus a
long tail — but the real value is foundational: it stops the generation of new
special cases (every new builtin currently requires another hardcoded branch),
and it is a prerequisite for the residual `Array.prototype.forEach/reduce`
"inherited index" semantics (those tests need `Object.prototype[1] = 1` to be
visible through `HasProperty`/`Get` on an array-like — Lever E's per-index
`HasProperty` check is already live per-iteration, so the prototype terminal is
the only missing piece for that cluster).

A scattered-storage observation worth recording: property descriptors are
stored as parallel `__getter_<k>` / `__setter_<k>` / `__attrs_<k>` entries
(four different variants of the scheme: TsMap, flat-overflow, array side-map,
native-object side-map; `TsObject.cpp:7443-8260`). It works, it's GC-safe (see
A3), but `getOwnPropertyDescriptor` must reconstruct descriptors from multiple
keys and attribute updates aren't atomic. Don't unify it now — but treat it as
the known tax every descriptor feature pays, and unify *if/when* Symbol-keyed
properties force a storage rework anyway (storage keys are hardcoded strings
today, so Symbol-keyed defineProperty is currently unrepresentable).

### A2. The materializer: keep it; direct iteration is a bad trade

`require_array_or_throw` (`TsObject.cpp:1948-2098`) snapshots `.length` once
and performs **all** indexed `[[Get]]`s eagerly before any callback runs,
filling absent indices with `undefined` and recording `originalReceiver`. The
observable deviations from the spec loop (`HasProperty(k)` → `Get(k)` →
callback, interleaved) are exactly three:

- getter side effects batch up-front instead of interleaving with callbacks;
- mid-iteration adds/deletes/length changes on the original are invisible to
  the *Get* (though `array_generic_absent_index` — `TsArray.cpp:205-220` —
  re-checks `HasProperty` on the original per iteration, so *deletions* are
  partially visible already);
- length is snapshotted (clamped at 1M).

Replacing materialization with direct receiver iteration would touch ~15
methods × ~3 code paths (fast/slow/closure) with **no shared iteration core**
— the loops are hand-rolled per method (`TsArray.cpp:869-1320`). That's 40+
independently breakable code paths, ~2× per-index cost (`has_prop` + `get` vs
a cached read), to win a bounded set of observability tests. The −42 trap
already demonstrated this tuning space is fragile. **Materialization +
per-method `HasProperty` patches (Lever E's approach) is the right call; don't
fund direct iteration.** If a specific interleaving cluster ever concentrates,
patch that one method the way Lever E did.

One real foot-gun found at the materializer/descriptor seam:
`array_index_write_intercept` (`TsArray.cpp:175`) — the function that honors
per-index setters/non-writable — is wired into only **two** call sites
(`TsArray.cpp:2303, 2522`, the dynamic set paths). All result-building methods
(`map`/`filter`/`slice`/`concat`/`flat`/`flatMap`) populate via `Push`/
`SetUnchecked` (a raw `((int64_t*)elements)[index] = value`, `TsArray.h:51`),
bypassing descriptors entirely. That is simultaneously the cause of the
"descriptor should be enumerable" cluster **and** the reason Lever 2 is safe:
the fix point is well-localized (see C).

### A3. Risk register: what the clustering lens under-weights

1. **Lever-A descriptor storage is GC-safe — confirmed.** `arr->properties` is
   a GC-allocated TsMap whose `TsHashTable` backing is allocated via
   `ts_gc_alloc_old_gen` with write barriers on value stores (`TsMap.cpp:140`,
   `TsHashTable.h:345-350`). No rooting gap there.
2. **`originalReceiver` fixup**: the field lives at the tail of the TsArray
   layout (`TsArray.h:161`) and *should* be covered by the promoted-object
   field scan (`TsGC.cpp:2617-2637`); if the scan boundary were ever off, a
   GC during a user callback would leave it stale. Cheap to verify once:
   `TS_GC_VERIFY=2` + an alloc-heavy `forEach.call(arrayLike, …)` probe.
   (A claimed timer-vector rooting gap was checked and is **false** — scanner
   + fixup registered at `EventLoop.cpp:80-81`.)
3. **`n in typedArray` is wrongly false — confirmed**: `ts_object_has_prop`
   (`TsObject.cpp:11039-11197`) has no TsTypedArray branch at all. It's
   currently *masked* by the TARR guard inside `array_generic_absent_index`
   (the G1 −17 lesson). It's a landmine: any future caller of HasProperty on a
   TA inherits the wrong answer. ~20-line fix, lets the special-case guard
   shrink to strings only.
4. **A systemic bucket the concentration metric structurally buries**: 506
   fails are "expected parse error but compiled successfully" spread over 75
   path prefixes — low concentration, so `cluster_fails.py` never surfaces it,
   yet it is *one* policy area (early-error enforcement in the front-end), not
   75 problems. Same shape: 1,141 "Expected a TypeError… no exception" across
   268 prefixes. Concentration is the right lens for *runtime semantics* bugs
   and the wrong lens for *policy* gaps.

---

## B. Methodology review

### B1. Signature clustering: verified accurate, with two correctable failure modes

The headline cluster sizes were independently recomputed and are **honest**:
"method invoked exactly once" = 426 (of which 424 are genuinely the
private-method/destructuring engine; 6 are signature-collision contaminants),
"descriptor should be enumerable" = 83, deleted-prototype-index = exactly 36.
The feared successive-assertion whack-a-mole effect was **measured at ~0.3%**
(of 1,722 expected-TypeError fails tracked over 4 days, 128 now pass, 1,590
fail identically, only 4 migrated signatures) — cluster counts are trustworthy
as flip estimates.

Two real problems:

1. **Out-of-scope bait at the top of the ranking.** 1,879 of 12,836 fails
   (14.6%) are plausibly AOT-out-of-scope (`staging/sm` 679, `annexB` 489,
   `intl402` 303, `with(` 241, `Function("` 127, sloppy top-level `this` 129),
   and **3 of the current top-8 concentration clusters are 100% unfixable**
   (91/91 sm-TypedArray-set, 96/96 annexB block-decl, 81/81 `with`). The tool
   actively surfaces work you've already decided not to do. Caveat: much of
   `staging/sm` is content-winnable plain ECMA-262 — tag it OOS-by-policy
   rather than deleting it from view.
2. **Low-concentration systemic buckets are invisible** (the 506 parse-error
   bucket above). Add a second ranking view: group by *fail-reason category*
   (parse-leniency, missing-TypeError, missing-ReferenceError, crash) in
   addition to signature.

### B2. The skip list is the biggest finding of this whole review

- **519 stale skips, zero-effort**: `run_test262.py:82-87` still denylists
  `resizable-arraybuffer` (400 tests), `WeakRef` + `FinalizationRegistry` (82,
  with comments referencing the *purged* Boehm GC), and `arraybuffer-transfer`
  (37) — all four features are marked implemented in the conformance matrices,
  and the runner's own harness setup *calls* `buffer.transfer()`.
- **5,106 tests skip on the `async` flag and 636 on `module`** — these are
  *harness* gaps ($DONE completion protocol / module compilation mode), not
  feature gaps; async/await and ES modules are implemented. This is the
  single largest recoverable pool in the entire suite. At even a conservative
  40-60% pass rate, wiring the async protocol is worth **~2,000-3,000 passes**
  — an order of magnitude more than any remaining semantic lever.

### B3. Gates: right arbiter, missing middle layer

The full sweep as arbiter is correct (the −117/−42 catches prove the fast
gates can't see exotic-array semantics). The missing layer is cheap:

- **Stratified ~2k subsample gate (~90 s at `--fast` throughput).** The runner
  needs only a `--paths-file` filter (~10 lines). Curate the sample from
  historically-regressed hot areas (Array.prototype iteration, property
  descriptors, class/dstr, property access), weighted toward *currently
  passing* tests. Back-of-envelope: the −117 incident (0.34% of executed)
  yields ~7 expected hits in even an unstratified 2k sample; stratification
  makes both prior incidents reliably detectable in ~2 minutes instead of ~20.
- **Per-test signature history sidecar**: append `(path, signature)` per sweep;
  annotate migrations in `cluster_fails.py`. Also fixes the dashboard blind
  spot where filtered micro-runs (the post-05-28 `pass 19/21` snapshot
  entries) are indistinguishable from full sweeps in `snapshots.jsonl`.
- **The ±6 noise floor has never actually been measured** — snapshots contain
  no per-test data and no same-code repeated full sweeps. Run the 2k sample
  twice back-to-back once (~3 min) and diff statuses to establish it
  empirically; timeouts (56 now vs 25-26 in May sweeps) are the most
  load-sensitive status and the likeliest flake source (concentrated in
  `built-ins/Promise/{all,allSettled,race}`).

---

## C. Recommendation

### C1. Ranking by yield ÷ risk ÷ effort

| rank | lever | yield | risk | effort | call |
|---|---|---|---|---|---|
| **0** | Harness/hygiene batch (stale skips, async `$DONE`, OOS partition, TA `in`, subsample gate) | **~2.5-3.5k** | very low | S-M | **fund first** |
| **1** | Lever 2: CreateDataPropertyOrThrow in result-building methods | ~83 | low-med | S (≈4-5 gated commits) | fund second |
| **2** | Lever 1: real `Object.prototype` terminal | ~36 direct + long tail + foundational | med (enumeration, hot path) | M | fund third |
| **3** | Lever 3: private-method-getter engine | ~424 | med-high (decl/expr lowering divergence + closure cells) | M-L, diagnosis-gated | time-boxed spike now, fix after |

The honest answer to "what gets the next 10k passes" is **not a semantic
lever** — no remaining in-scope cluster exceeds ~424, and the sum of all three
named levers is ~550. The 10k path is: async-flag harness (~2-3k) + module
flag (~300-400) + stale skips (~300-400) + Object.prototype foundation
unlocking the inherited-property long tail + the parse-error/early-error
policy bucket (~300-500) + Lever 3 (~424) + steady cluster grinding. Fund the
harness work like an architectural lever, because it is one.

### C2. Decomposition of the funded picks (each step full-sweep-gated per house rules)

**Batch 0 — hygiene (each independently committable):**
1. Delete 4 stale `UNSUPPORTED_FEATURES` entries (`run_test262.py:82-87`).
   Expected: +several hundred executed, net pass increase; risk: sweep time
   +519 tests (negligible). Detect: the sweep itself.
2. `--paths-file` + curated 2k stratified sample + run-twice noise-floor
   measurement. No product risk. Becomes the new pre-sweep gate for every
   later step.
3. OOS tagging in `cluster_fails.py` (path + source heuristics; label, don't
   hide) + a reason-category ranking view. No product risk.
4. TypedArray branch in `ts_object_has_prop` (dense indices `0 ≤ n < length`
   true, walk side-map otherwise); then shrink the TARR guard in
   `array_generic_absent_index` to a comment. Risk: the G1-style skip
   regression — detect with the 2k sample (it covers Array.prototype generic
   receivers) before the sweep.
5. Async `$DONE` protocol in the runner (wire `doneprintHandle.js`, treat
   async-flag tests as runnable, gate completion on the printed sentinel).
   Risk: timeout inflation from genuinely-broken async tests — set a
   conservative per-test timeout and a separate sweep lane first time.

**Lever 2 — CreateDataPropertyOrThrow (~83):**
1. Add `ts_array_create_data_property(result, i, v)` in `TsArray.cpp`:
   fast-path `result->properties == nullptr` → raw store (zero cost for the
   99.9% case); otherwise honor `__arr_attrs_/`accessor side-map with
   *define* semantics (no setter invocation, override non-writable per
   CreateDataProperty). Commit with golden-IR + node + 2k sample.
2-5. Convert `map`/`filter` (`TsArray.cpp:949-1089`), then `slice`
   (`:1714-1738`), then `concat` (`:3473-3592`), then `flat`/`flatMap` +
   `Array.from`/`of`, one full-sweep-gated commit each. Expected flips
   concentrate in `built-ins/Array/from/*-set-elem-prop-non-writable` and the
   per-method `create-*` families. Regression risk per step: result-array
   population perf and the species path — the species slow path
   (`ts_array_species_rematerialize`, `TsArray.cpp:3272-3304`) already uses
   `ts_object_set_property` and is correct as-is ([[Set]] on a custom-species
   result is spec); don't touch it.

**Lever 1 — Object.prototype terminal (staged to keep each step revertible):**
1. Create the rooted singleton in `TsGlobals.cpp`, populate builtins with
   `enumerable=false` attrs; *nothing consults it yet*. Zero-risk commit.
2. Wire it as the chain terminal in the TsMap walk only
   (`ts_object_get_property:4372`, `has_prop:11122`), keeping hardcoded
   fallbacks. Risk: for-in leakage — detect with an enumeration probe matrix
   + lodash suite + 2k sample before sweeping.
3. Make `getPrototypeOf({})` return the singleton (identity test flips).
4. Add the walk to flat objects, then arrays (one commit each — these are the
   hot-path commits; sweep is the arbiter).
5. Only then start deleting hardcoded branches, one receiver-type per commit.

**Lever 3 — diagnosis spike first (time-boxed, no-commit):** compile
`tmp/privg.js` in declaration and expression forms with `--dump-ir` and diff —
the audit localized the divergence to declaration-path method lowering vs the
expression pre-pass (`ASTToHIR.cpp:678` pre-pass, `:11203`
visitClassDeclaration, `:11750` visitClassExpression cache, `:8899`
GetPropStatic emission for `this.#m`-as-value), with the leading hypothesis
being private-name resolution falling through to a dynamic property read that
can't see the mangled method. The IR diff should confirm or kill that in an
hour without cdb; if confirmed, the fix is a lowering change (resolve `#m`
reads through the same mangled-name path calls use, binding `this`), gated by
a privg/privm probe matrix + golden-IR before each sweep.
