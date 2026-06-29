# CONF-P2 — Iterator Protocol Completion

**Status:** Scoped / not started
**Phase:** Roadmap Phase 2 (iterator protocol)
**Owner:** TBD
**Baseline:** master `a019e41a`, fresh sweep 33,801 / 50,506 (66.9%)
**Related:** GEN-001 (suspendable generators — owns the generator/async-generator portion); CONF-P1
(builtin metadata — some per-family iterator fails are metadata, not iterator, bugs)

---

## 0. TL;DR and honest sizing

Like P1, the assumption "iterators are broken, fixing them is worth ~500–800" is **too coarse**.
The iterator *infrastructure already exists and largely works*:

- Real iterator factories with state + per-type prototypes — `ts_create_array_iterator` /
  `ts_create_map_iterator` / `ts_create_set_iterator`, built on `%ArrayIteratorPrototype%` /
  `%MapIteratorPrototype%` / `%SetIteratorPrototype%` (`TsMap.cpp:1136-1318`), each with a real
  `.next()` returning `{value, done}` and `[Symbol.toStringTag]`.
- The compiler routes `for-of`, array/call spread, and array destructuring through the **spec
  protocol** (`ts_iterator_get` → `ts_iterator_next` → read `{value,done}`; `TsPromise.cpp:1240`,
  `TsArray.cpp:2664/2708`). **Custom iterables work today.** Array destructuring even performs a
  spec-correct **IteratorClose** (`.return()`, `TsArray.cpp:2768-2799`).
- **Map.prototype.values/keys/entries pass 100%** (10/10 each) — proof the model is sound.

The failures are concentrated, not systemic. Sizing from the fresh sweep:

| Cluster | Numbers | Owner |
|---|---|---|
| **Generators (sync)** | Generator 46 pass / **84 FAIL** | **GEN-001**, not P2 |
| **Async iterator/generator** | 20 pass / **56 FAIL** + ~88 async-SameValue | **GEN-001** + W-async (P2 §W6 optional) |
| **Iterator Helpers proposal** (`built-ins/Iterator/*`) | 75 pass / 76 fail / **575 SKIP** | P2 §W7 (optional, big, mostly-skipped) |
| **String iterator method** | `String/.../Symbol.iterator` **0 pass / 6** | **P2 W2** |
| **Set values/keys/entries** (compiler typed-path leak) | ~7 fail (`set.values().next` undefined) | **P2 W3** |
| **`%IteratorPrototype%` root chain** | per-type prototypes are siblings | **P2 W1** |
| **for-of IteratorClose / poisoned-iterator** | subset of language `for-of` (216 fail) | **P2 W4** |
| Array/TypedArray values/keys/entries edges | ~12 + ~10 fail (part metadata → P1) | P2 W5 / P1 |
| ~61 **CRASHES** in iterator/generator paths | VectoredException ×61 | mostly GEN-001; triage in P2 |

**Revised P2 (core, W1–W5) yield estimate: ~+150–350.** The generator volume is GEN-001's;
Iterator Helpers (W7) would un-skip up to ~575 but is a large standalone build — recommend its
own ticket. So P2's *core* is a focused protocol-completion, with two big optional extensions
gated behind explicit go/no-go.

---

## 1. What works today (do not rebuild)

| Capability | Status | File:line |
|---|---|---|
| Iterator result `{value,done}` (fresh TsMap per `.next`) | ✅ | `TsMap.cpp:1248` |
| `%ArrayIteratorPrototype%` / `%MapIteratorPrototype%` / `%SetIteratorPrototype%` | ✅ | `TsMap.cpp:1136-1189` |
| `ts_create_{array,map,set}_iterator` factories | ✅ | `TsMap.cpp:1303-1318` |
| Array/Map/Set/TypedArray `.values/.keys/.entries` (JS proto methods) | ✅ real iterators | `TsObject_Builtins.cpp:1519`, `TsGlobals.cpp:511/2371/2474/5331` |
| `X.prototype[@@iterator] === X.prototype.values/entries` identity | ✅ | `TsGlobals.cpp:515/2430/2498` |
| `ts_iterator_get` / `ts_iterator_next` / result accessors | ✅ | `TsPromise.cpp:1240/1554/1589/1605` |
| for-of / spread / destructuring route through protocol; custom iterables | ✅ | `ASTToHIR_Statements.cpp:222`, `ASTToHIR_Expressions_*`, `TsArray.cpp:2664/2708` |
| Array destructuring IteratorClose (`.return`) | ✅ spec-correct | `TsArray.cpp:2768-2799` |

---

## 2. Work items (core P2)

### W1 — `%IteratorPrototype%` root + reparent the per-type prototypes
**Gap:** `%ArrayIteratorPrototype%` / `%MapIteratorPrototype%` / `%SetIteratorPrototype%` are
*siblings*, each with their own `next` + `[Symbol.iterator]`. Spec requires they inherit from a
single `%IteratorPrototype%` (ECMA-262 27.1.2) whose `[Symbol.iterator]() { return this }` they
share. Tests assert the chain (`Object.getPrototypeOf(Object.getPrototypeOf(arr.values()))`), and
`%IteratorPrototype%` is the future home of the Iterator Helpers (W7).
**Fix:** in `buildIteratorPrototype` (`TsMap.cpp:1136`), create a single rooted
`%IteratorPrototype%` (with `[Symbol.iterator]→this`), then `SetPrototype(%IteratorPrototype%)` on
each per-type proto and move `[Symbol.iterator]` up to the root. GC-root the new object (copy the
existing tenure/register pattern at `TsMap.cpp:1159`).
**Files:** `TsMap.cpp:1136-1189`. **Est.** ~+15–40 (chain/`Symbol.iterator`-identity tests across
Array/Map/Set/TypedArray). **Risk:** low; foundational — do FIRST.

### W2 — `String.prototype[Symbol.iterator]` (+ `%StringIteratorPrototype%`)
**Gap:** the compiler can iterate a string via a typed path, but there is **no
`String.prototype[Symbol.iterator]` method** (`String/.../Symbol.iterator` 0/6) and no
`%StringIteratorPrototype%`. So `""[Symbol.iterator]()`, `[...str]` via the method, and
`Array.from(str)`-style code paths that fetch the method fail.
**Fix:** add a code-point-aware string iterator (advance by full code point, surrogate pairs as one
step), a `%StringIteratorPrototype%` (`@@toStringTag` "String Iterator", inherits `%IteratorPrototype%`
from W1), and install `String.prototype[Symbol.iterator]`. A `ts_create_string_iterator(structure)`
factory mirroring the array one.
**Files:** `TsGlobals.cpp` String section (~764-912), new factory near `TsMap.cpp:1303`.
**Est.** ~+10–25 (String/Symbol.iterator + string for-of-via-method + a few Array.from/spread).
**Risk:** low–medium (code-point boundary correctness; reuse `s->CharCodeAt`/`CodePointAt`).

### W3 — `Set.values/keys/entries` JS-facing call returns a real iterator
**Gap:** `new Set([1,2]).values().next` is `undefined` — the **compiler lowers `set.values()` to the
typed `ts_set_values` TsArray fast-path** (the "typed path returns a TsArray so it can be indexed
directly" optimization) instead of the proto method `ts_set_values_iter_wrapper`, which *does*
return a real `%SetIteratorPrototype%` iterator. (Map does NOT have this leak → Map passes.)
**Fix:** make the JS-facing `set.values()/keys()/entries()` lower to the real-iterator wrapper. Two
options: (a) in the typed lowering (`BuiltinRegistry.cpp` / the Set method dispatch), route these
three to the iterator wrapper, keeping the TsArray path only for internal typed for-of where the
result is immediately consumed; or (b) make `ts_set_values` return a real iterator and update the
typed for-of consumer. Verify typed `for (const x of set)` still works and stays fast.
**Files:** compiler Set-method lowering (`BuiltinRegistry.cpp` + `ASTToHIR`/`HIRToLLVM` method
path), `TsSet.cpp:551-590`. **Est.** ~+7–15 (Set returns-iterator + `.next` + downstream
Array.from(set.values()) etc.). **Risk:** medium — it's the runtime/compiler seam; gate the typed
for-of microbench + golden-IR.

### W4 — for-of IteratorClose on abrupt completion
**Gap:** for-of does **not** call `iterator.return()` on `break`/`return`/`throw` (only array
destructuring does). Spec (ECMA-262 14.7.5.6) requires IteratorClose on abrupt completion. Part of
the language `for-of` failures (216) test this (`*/iterator-close*`, `*/return-*`).
**Fix:** in the for-of lowering (`ASTToHIR_Statements.cpp:222-727`, the protocol path), wrap the
loop so that an abrupt exit invokes the iterator's `return` method (if present) and propagates the
original completion; honor the spec ordering (return value ignored unless it throws). NB: the
array index fast-path (`:536-727`) never created an iterator, so it needs no close — but verify a
for-of over an array with a *custom* `@@iterator` takes the protocol path (the own-property check
at `ASTToHIR.cpp:1364` suggests it does).
**Files:** `ASTToHIR_Statements.cpp:222-727`, possibly a `ts_iterator_close` runtime helper.
**Est.** ~+20–60 (iterator-close + the dstr/for-of close family). **Risk:** medium (control-flow
lowering; golden-IR + node gate).

### W5 — Arguments `[@@iterator]` + per-family iterator-result edges
**Gap:** `arguments[@@iterator]` missing (arguments not iterable); a few Array/TypedArray
values/keys/entries edge fails are iterator-result detail (and some are metadata → P1).
**Fix:** install `arguments` `@@iterator` (reuse the array iterator over the arguments backing);
triage the ~12 Array + ~10 TypedArray iterator fails, routing metadata ones to P1.
**Files:** arguments construction (search `isArguments`), `TsGlobals.cpp` TypedArray section.
**Est.** ~+10–20. **Risk:** low.

---

## 3. Optional extensions (explicit go/no-go — recommend separate tickets)

### W6 — Async iterator chain (`%AsyncIteratorPrototype%`, `for-await` on async iterables)
Overlaps **GEN-001**. The async-iterator prototype + `Symbol.asyncIterator` + AsyncFromSyncIterator
wrapping. ~56 AsyncIterator + ~88 async-SameValue fails, but most are async-*generator* (GEN-001).
**Recommendation:** fold the async-iterator *prototype/chain* piece into GEN-001; do not open in P2.

### W7 — Iterator Helpers proposal (`built-ins/Iterator/*`)
`Iterator.prototype.{map,filter,take,drop,flatMap,reduce,toArray,forEach,some,every,find}`,
`Iterator.from`, `%WrapForValidIteratorPrototype%`, the `Iterator` global. **726 tests: 75 pass,
76 fail, 575 SKIP** (the runner feature-flags them). Implementing the proposal lands on W1's
`%IteratorPrototype%` and could un-skip up to ~575 — the single largest iterator opportunity — but
it's a substantial standalone build (each helper is a lazy iterator).
**Recommendation:** **own ticket (CONF-P2b)**, scheduled after W1; decide based on whether the
runner's skip-flag is flipped (un-skipping is required to realize the 575).

---

## 4. Scope

**IN (core):** W1 `%IteratorPrototype%` root, W2 String iterator, W3 Set typed-path leak, W4 for-of
IteratorClose, W5 Arguments iterator + result-edge triage.

**OUT:**
- **Generators (sync + async): GEN-001.** The 84 Generator + 56 AsyncIterator fails + ~61 crashes
  in generator paths are GEN-001's rearchitecture, not P2. P2 only ensures the *protocol* a
  generator plugs into is correct.
- **Iterator Helpers: CONF-P2b** (W7, after W1).
- **Async-iterator chain: GEN-001** (W6).
- Metadata-shaped iterator fails (length/name/desc on iterator methods) → **CONF-P1**.
- The bulk of language `dstr` (653) and `for-await-of` (191) fails are NOT iterator-protocol
  (default-value order, TDZ, computed keys, async binding) — do not claim them for P2.

---

## 5. Measurement & gates

**Targeted baselines (NARROW `--filter`, common-set vs `.realbaseline.jsonl`):**
`Array/prototype/Symbol.iterator`, `String/prototype/Symbol.iterator`, `Set/prototype/values`,
`Map/prototype` (regression guard — already 100%), `language/statements/for-of`,
`built-ins/IteratorPrototype`, `built-ins/Iterator` (for W7 go/no-go).

**Gate every commit (run SEPARATELY — combined chain exceeds the foreground cap):**
1. build (taskkill `ts-aot.exe`; copy `tsruntime_shared.dll` → `tests/test262/`).
2. golden-ir 267/279 (W3/W4 touch lowering — watch this closely).
3. node 295/297.
4. 2k `regression_sample.txt` via `.gate_base.pkl`, 0 REAL lost.
5. per-family `--filter` GAINED/LOST; inspect every LOST `-j1`.
6. **W3/W4 perf guard:** a dense-array + Set for-of microbench must not regress (the typed
   fast-path must survive).

---

## 6. Sequencing, risks, dependencies

- **Order: W1 → W2 → W5 → W3 → W4.** W1 (the root prototype) is foundational and unblocks W2/W7.
  W3 and W4 are the compiler-seam items — do them after the pure-runtime wins so a regression is
  easy to bisect.
- **Biggest risk is W3/W4** (runtime↔compiler seam + control-flow lowering). Both must preserve the
  array/Set typed fast-paths or hot loops regress. Gate golden-IR + the microbench on each.
- **Dependency on GEN-001:** P2 and GEN-001 are complementary — P2 makes the *protocol surface*
  spec-correct; GEN-001 makes *generators* suspend correctly. Land W1 before GEN-001 reworks async
  iteration so both build on one `%IteratorPrototype%`.
- **W7 is the real volume** (≤575) but mostly-skipped — its ROI depends on flipping the runner skip
  flag; treat as a separate, explicitly-budgeted project.

---

## 7. Roadmap feedback

Confirms the P1 lesson: the *infrastructure is more complete than the failure counts suggest*. P2's
**core** is a ~+150–350 protocol-completion, not a 500–800 rebuild. The genuinely large iterator
opportunity is **Iterator Helpers (W7, ≤575 mostly-skipped)** and **generators (GEN-001, ~140
fails)** — both already partially scoped elsewhere. Net: keep P2 core small and surgical (W1–W5),
and route the volume to GEN-001 + a dedicated CONF-P2b for Iterator Helpers.

---
## STATUS UPDATE (2026-06-29)
**W1 DONE** — %IteratorPrototype% root added (TsMap.cpp), per-type prototypes reparented; **W2 DONE** — String.prototype[Symbol.iterator] + %StringIteratorPrototype%; **W3 DONE** — Set values/keys/entries now return real %SetIteratorPrototype% iterators (typed-path leak closed via BuiltinRegistry + ts_set_*_iterator wrappers).
**W7 DONE (was scoped to a separate CONF-P2b)** — full Iterator Helpers proposal: eager (toArray/forEach/reduce/some/every/find) + lazy (map/filter/take/drop/flatMap) + Iterator.from. **Iterator went 0→153 passing.** Generators inherit %IteratorPrototype% so helpers work on them (+73).
**REMAINING:** W4 (for-of IteratorClose on break/return/throw) and W5 (arguments @@iterator + result-edge triage). W6 (async-iterator chain) is GEN-001's. Recommend narrowing this ticket to W4/W5 or splitting them into a small follow-up and archiving.
