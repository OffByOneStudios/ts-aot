# test262 Conformance Results

**Last sweep:** 2026-05-15 (commit `299cf0f` — Track A round 2)
**Runner:** `python tests/test262/run_test262.py --fresh -j 24 --timeout 8`
**Suite version:** vendored at `tests/test262/test262/`

## Current Status — ce ZERO + Track A receiver guards landed

| Status | Count | % of total | % of executed |
|--------|------:|-----------:|--------------:|
| **pass** | 17,222 | 34.10% | 49.40% |
| **fail** | 17,602 | 34.85% | 50.49% |
| **compile_error** (ce) | **0** | **0%** | **0%** |
| **timeout** | 37 | 0.07% | 0.11% |
| **crash** (subset of fail) | 564 | — | 1.62% |
| skip | 15,645 | 30.98% | — |
| **Total** | 50,506 | 100% | 34,861 executed |

**Executed pass rate: 49.4%** (17,222 / 34,861).

**Track A round 1 + 2 cumulative impact (3 commits, 2026-05-15):**
- Pass: 17,176 → **17,222 (+46)**
- Fail: 17,648 → **17,602 (-46)**
- TypedArray.prototype crashes: 52 → 48 (-4 directly; getter-extraction tests for `this-is-not-object` and `this-has-no-typedarrayname-internal` now pass)
- Plus ArrayBuffer.prototype getter-extraction tests now pass (`built-ins/ArrayBuffer/prototype/{byteLength,detached,resizable,maxByteLength}/this-is-not-object.js`)

The compile-error tail is now empty. The compiler successfully translates every non-skipped test in the test262 corpus to valid LLVM IR.

## Trajectory — 2026-05-14/15 day (17 commits, ce 42 → 0)

Three pushes spanning ASan-detected memory-corruption forensics, parser scoping refactors, codegen defensive guards, and a small ICU-fallback table. All 17 commits landed with 0 golden_ir/node regressions.

| Phase | Commits | ce Δ | Key wins |
|-------|--------:|-----:|----------|
| Push 1: ASan forensics | 2 | -26 | HIRClass UAF (`3839367`), getter/setter null (`8fe9f49`) |
| Push 2: Finish-the-tail | 8 | -6 | BuiltinResolution UAF, lowerStore/lowerFunction guards, parser scope push |
| Push 3: Last-mile | 7 | -10 | 0-yield generator SSA, Unicode 16, Math no-arg guards, skip mechanism |

Full commit list in [`memory/session_2026-05-14_finish_the_tail.md`](../../C:/Users/cgrin/.claude/projects/E--src-github-com-cgrinker-ts-aoc/memory/session_2026-05-14_finish_the_tail.md) and `memory/MEMORY.md` index.

## Next Targets — Top Failure Clusters

Two ways to slice the 17,648 fails. By **path prefix** to find feature/area gaps; by **reason pattern** to find systemic semantic issues.

### Top 25 fail clusters by 3-segment path prefix

| Count | Cluster | Likely dominant cause |
|------:|---------|------------------------|
| 2,092 | `language/statements/class` | Class semantics (likely a mix: TDZ, static, private, generator method) |
| 1,361 | `language/expressions/class` | Same as above, expression form |
| 970 | `built-ins/Array/prototype` | Array method spec edge cases |
| 543 | `language/expressions/object` | Object literal / method semantics |
| 499 | `built-ins/TypedArray/prototype` | TypedArray method edges + TypeError detection |
| 496 | `built-ins/String/prototype` | String method spec edges |
| 494 | `built-ins/Object/defineProperty` | Descriptor semantics, throwing on invalid descriptors |
| 453 | `language/statements/for-of` | dstr / iteration protocol edges |
| 391 | `built-ins/RegExp/prototype` | RegExp method edges |
| 304 | `language/expressions/assignment` | Destructuring / compound semantics |
| 300 | `built-ins/Object/defineProperties` | Multiple-descriptor edge cases |
| 275 | `built-ins/DataView/prototype` | Endianness / OOB / detached buffer cases |
| 235 | `language/expressions/compound-assignment` | `+=`/etc. with property access (TypeError paths) |
| 216 | `built-ins/Function/prototype` | bind/call/apply edges |
| 192 | `language/expressions/generators` | Generator-expression semantics |
| 192 | `language/statements/function` | Function semantics |
| 190 | `language/expressions/function` | Same, expression form |
| 184 | `language/statements/for` | for-statement edge cases |
| 172 | `built-ins/TypedArrayConstructors/internals` | Constructor edge cases |
| 170 | `built-ins/RegExp/property-escapes` | RegExp \p{} edges |
| 155 | `language/statements/generators` | Generator semantics |
| 151 | `language/expressions/arrow-function` | Arrow semantics |
| 136 | `built-ins/Object/prototype` | Object.prototype methods |
| 135 | `language/statements/with` | `with` statement (parsed, never lowered) |
| 132 | `built-ins/Date/prototype` | Date method edges |

### Top failure reason patterns (deduplicated)

| Count | Pattern | What this means |
|------:|---------|-----------------|
| **1,375** | `Uncaught: Expected a TypeError to be thrown but no exception was thrown at all` | Runtime didn't throw a required TypeError. Likely missing receiver/arg validation in stdlib methods. |
| **993** | `Uncaught: Expected a Test262Error to be thrown but no exception was thrown at all` | Test's `assert` block passed silently instead of throwing on a wrong value. Almost always indicates the prior assertion DID get the right value, so a different test step misbehaved. |
| **963** | `expected parse error but compiled successfully` | Negative test that asserts a SyntaxError — our parser is too permissive. |
| **871** | `Expected SameValue(undefined, N)` | Runtime returned undefined where a number was expected. Common cause: method returning undefined when it should return a primitive. |
| **666** | `Expected SameValue(N, N)` (numeric mismatch) | Computed result differs from spec. |
| **649** | `Expected SameValue("X", "X")` (string mismatch) | String result differs. |
| **599** | `Expected SameValue([object Object], [object Object])` | Object identity check failure — likely wrong object returned (e.g., `this` vs receiver). |
| **569** | `CRASH: VectoredException 0xc0000005` | Native access-violation crashes at runtime (not compile-time). |
| **540** | `Expected SameValue(undefined, "X")` | Method returned undefined where string was expected. |
| **530** | `Uncaught: Expected a ReferenceError to be thrown` | Tests checking TDZ, undeclared vars, etc. — runtime doesn't ReferenceError consistently. |
| 314 | `AssertionError [ERR_ASSERTION]: ...falsy value` | Generic assertion failure (varied causes). |
| 280 | `Expected true but got false` | Boolean assertion failure. |
| 136 | `An initialized binding is not created prior to evaluation` | TDZ semantics (re-binding before init). |

### Top CRASH clusters (569 crashes — runtime segfaults)

| Count | Cluster |
|------:|---------|
| 52 | `built-ins/TypedArray/prototype` |
| 36 | `language/expressions/yield` |
| 35 | `language/statements/class` |
| 33 | `language/expressions/class` |
| 29 | `language/statements/for-of` |
| 26 | `built-ins/String/prototype` |
| 23 | `built-ins/Iterator/concat` |
| 20 | `built-ins/Function/prototype` |
| 19 | `language/expressions/object` |
| 19 | `language/statements/with` |
| 17 | `language/statements/async-generator` |

Runtime crashes are real correctness bugs in the generated code or runtime — these are higher priority than `fail` because they suggest memory safety issues in the compiled binaries (not in `ts-aot.exe` itself).

### Top parse-negative clusters (963 — should be SyntaxError but compile)

| Count | Cluster |
|------:|---------|
| 163 | `built-ins/RegExp/property-escapes` |
| 109 | `language/statements/class` |
| 95 | `language/literals/regexp` |
| 79 | `language/expressions/class` |
| 63 | `language/block-scope/syntax` |
| 42 | `language/expressions/dynamic-import` |
| 34 | `language/expressions/object` |
| 29 | `built-ins/RegExp/prototype` |
| 27 | `language/literals/string` |
| 25 | `language/statements/for-in` |
| 23 | `language/statements/for-of` |

The two RegExp clusters (163+95+29 = 287) are mostly invalid regex syntax we accept — many of these are easy parser-side rejections.

## Recommended Next-Target Priorities

Three tracks ranked by expected ROI per hour of work:

### Track A — Runtime TypeError emission (~1,375 tests, high ROI)

The single biggest cluster. Most stdlib methods need a `RequireObjectCoercible` / `ThisHasInternalSlot` / "method called on non-object" check that throws TypeError before doing work. We've already done this for some Array/String methods (commits `27/27` and `23/23` from prior sessions). Audit the remaining built-ins systematically:

- TypedArray.prototype methods (52 + many fails are likely missing receiver checks)
- DataView.prototype methods (275 fails, lots of receiver-validation gaps)
- Object.defineProperty / defineProperties (794 fails combined — descriptor validation)

**Estimated impact: +300 to +600 pass** depending on cluster overlap.

### Track B — Parser-side negative-test detection (~963 tests, medium ROI)

Tests asserting SyntaxError that our parser doesn't catch. Three sub-clusters:

1. **RegExp body validation (287 tests):** `\p{NotAProp}`, illegal flags, invalid character class. Mostly ICU/regex-validation work.
2. **Class early errors (188 tests):** duplicate constructor, invalid static field, etc.
3. **Block-scope redeclaration (63 tests):** sibling-block dup detection.

**Estimated impact: +200 to +400 pass.**

### Track C — Runtime crash investigation (~569 crashes, high importance lower ROI)

These are real correctness bugs — generated code crashes on valid input. Per-cluster forensics under CDB or ASan-instrumented runtime. The TypedArray + iterator clusters (75+) suggest issues with detached-buffer or iterator-result-shape handling.

**Estimated impact: +100 to +200 pass, plus reduced crash-blast risk in production.**

### Track D — Smaller surgical wins

- **`with` statement (135 fails + 19 crashes):** Either implement minimal `with` lowering (low ROI per LOC) or skip those tests (some are negative tests checking strict-mode rejection — those would still need parser work).
- **Date.prototype (132 fails):** mostly locale/format edge cases. ICU calls.
- **Bigger pictures: ICU 76 upgrade.** Unlocks Unicode 16.0 (currently faked via fallback table) and likely modernizes locale/format for Date/Intl.

## Methodology Notes

Carrying over from the autonomous-loop ce-cluster era:

- **Sweep noise floor is ±6 to ±15 pass.** Same-commit re-runs swing within this band from timeout scheduling / intl402 flake.
- **Trust per-cluster counts more than overall pass deltas.** A targeted fix for cluster X should show movement in cluster X's count specifically.
- **ASan is the right forensic tool for "non-deterministic compile failure".** CDB usually can't reproduce; ASan deterministically catches the UAF on first invocation. See `memory/asan-build-tradeoffs.md` and `memory/hirtollvm-segfault-investigation.md` for the canonical playbook.
- **Spec-cite every commit.** Each commit message references the ECMA-262 section being implemented. This discipline rules out cargo-culted "fixes" that match a single engine's quirk rather than the spec.

## Re-Running the Sweep

```bash
python tests/test262/run_test262.py --fresh -j 24 --timeout 8 2>&1 | tail -5
```

Results are written to `tests/test262/.test262_results.jsonl`. Cluster failures with:

```python
import json, collections
fails = []
with open('tests/test262/.test262_results.jsonl','rb') as f:
    for raw in f:
        try:
            r = json.loads(raw.decode('utf-8','ignore'))
            if r.get('status') == 'fail':
                fails.append(r.get('path','').replace(chr(92),'/'))
        except: pass
c = collections.Counter()
for p in fails:
    c['/'.join(p.split('/')[:3])] += 1
for k,v in c.most_common(25): print(f'{v:5d}  {k}')
```
