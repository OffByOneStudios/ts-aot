# test262 Conformance Results

**Last sweep:** 2026-05-12 (commit `8e08f14`)
**Runner:** `python tests/test262/run_test262.py --fresh -j 24 --timeout 8`
**Suite version:** vendored at `tests/test262/test262/`

## Current Status

| Status | Count | % of total | % of executed |
|--------|------:|-----------:|--------------:|
| **pass** | 17,179 | 34.01% | 49.62% |
| **fail** | 17,482 | 34.61% | 50.49% |
| **compile_error** (ce) | 164 | 0.32% | 0.47% |
| **timeout** | 37 | 0.07% | 0.11% |
| skip | 15,644 | 30.97% | — |
| **Total** | 50,506 | 100% | 34,862 executed |

Executed pass rate: **49.6%** (17,179 / 34,862).

**Session-cumulative change** (29 commits since 2026-05-11):
- Pass: 17,137 → 17,179 (**+42**)
- Ce: 384 → 164 (**-220, 57% reduction**)
- Highest-yield commit: `b53f3ae` + `8e08f14` monomorphizer stub fallback (-92 ce, +18 pass).

The skip count covers tests for features explicitly marked unsupported (modules, `eval`, BigInt edge cases, `tail-call-optimization`, etc.) via the runner's feature-flag filter.

## Trajectory (recent commits)

The last 27 commits (May 11–12, 2026) drove the autonomous-loop ce-cluster spike. Starting baseline was pass 17,137 / ce 384.

| Metric | Start | Current | Δ |
|--------|-------|--------:|--:|
| Pass | 17,137 | 17,161 | **+24** |
| Ce | 384 | 256 | **-128 (-33%)** |

Highest-yield individual commits:
- `64ce932` ComputedPropertyName in ObjectBindingPattern → **-67 ce**
- `a269591` Function param flags scoping (inner function's `[Await]`/`[Yield]`) → **+13 pass**
- `1ed98d1` Class expression extends parity with class declaration → **-12 ce**
- `392e949` Intl prototype.constructor backref → **+9 pass**
- `15aeb5c` Top-level await tightening → **-8 ce**
- `df536c5` Contextual keywords (constructor/keyof/etc.) as let-binding identifiers → **-8 ce**
- `f560eb6` Escape-encoded strict-FRWs as BindingIdentifier in non-strict → **+5 pass**

Sweep noise floor: **±6 pass** on same-commit re-runs (measured via diagnostic resweep). Single-commit deltas within that band are not deterministic.

## Compile-Error Clusters (top by 3-segment path prefix)

| Count | Cluster | Dominant cause |
|------:|---------|----------------|
| 23 | `language/statements/class` | Generator-method yield as binding, computed-property from yield |
| 21 | `staging/sm/expressions` | SM-specific harness deps (`print`, `assertEq`) + destructuring |
| 18 | `language/statements/for-of` | dstr array-elem iter-close patterns (SSA dominance) |
| 16 | `annexB/language/eval-code` | Runtime `eval` (AOT-incompatible) |
| 16 | `language/expressions/assignment` | dstr rest-elem yield-expr (SSA dominance) |
| 14 | `language/expressions/class` | Same yield/private patterns as class statements |
| 12 | `language/expressions/object` | yield-expr in method names |
| 11 | `language/expressions/in` | `#x in y` private-field brand check (feature) |
| 9 | `staging/sm/TypedArray` | Harness arity mismatch (`assertThrowsInstanceOf`) |
| 8 | `staging/sm/RegExp` | Harness deps + arity mismatch |
| 7 | `staging/sm/class` | Default constructor as function, derived class TDZ |
| 7 | `staging/sm/Set` | SSA dominance in Set.prototype.union/etc. |

## Heavy-Cluster Root Causes (verified via research agents 2026-05-12)

The 256 remaining ce tests cluster around five heavy infrastructural issues. Narrow parser fixes are exhausted; further progress requires investments outside the narrow-fix template.

### 1. Monomorphizer `_any` specialization not emitted (~80 tests)

**Symptom:** `ts-aot-linker: error: undefined symbol: print_any` (or `Proxy_any_any`, `WeakSet_any`, `reportFailure_any`, `__func`).

**Root cause:** three-part. When a function is called with `any`-typed args, the call-site mangler emits `<name>_any` as a static LLVM symbol, but the definition is never emitted because:

- For built-in globals (Proxy, WeakSet, Map, Set, Reflect, Promise, typed-array constructors): the call-position dispatch in `ASTToHIR.cpp:6571-6603` has no case for them and falls through to user-function mangling. The identifier-resolution path (`ASTToHIR.cpp:8134-8157`) recognizes them but the call path doesn't.
- For harness JS functions (`print`, `assertEq`, `reportFailure` from test262 harness/sm/non262.js): the JS-untyped module rewriter at `Monomorphizer.cpp:1127` moves FunctionDeclarations into a synthetic `moduleInit`, but `findFunction` at `:2164` only walks `analyzer.modules[*]->ast->body` and never searches `syntheticFunctions`, so the spec request silently fails.
- For genuinely-unknown call targets: the generic Call lowering at `HIRToLLVM.cpp:5396-5415` emits an `ExternalLinkage` forward declaration with no body, instead of a stub returning `undefined`.

**Possible fixes (one or more):**
- **A. Stub emission in `lowerCall` (cheapest).** Mirror the existing `lowerLoadFunction` stub pattern at `HIRToLLVM.cpp:5413`: emit `InternalLinkage` function with a body that returns `ts_value_make_undefined()`, preserving the mangled name verbatim. Turns link-time failures into runtime ReferenceErrors. **Est. blast radius: ~80 tests** (covers all three sub-causes).
- **B. Builtin-call-without-new dispatch (most principled).** Add Proxy/WeakSet/Map/Set/Reflect/Promise/etc. to the call-position handler in `ASTToHIR.cpp:6606`, lowering as `createCallIndirect(createLoadGlobal(name), args, Any)`. **Est. blast radius: ~40 tests, mostly under built-ins/Proxy, built-ins/WeakSet, built-ins/WeakMap.**
- **C. findFunction also searches `syntheticFunctions`.** Fixes only the harness-JS-function case. **Est. blast radius: ~20 tests under staging/sm/*.**

A+B+C combined would unlock most of the ~80 monomorphizer-_any cluster. Recommendation: start with A (single-site stub emission), measure, then layer B for the builtin TypeError tests.

### 2. Generator/yield SSA dominance (~80 tests)

**Symptom:** `LLVM Module verification failed: Instruction does not dominate all uses!` in generator bodies.

**Root cause:** generator state-machine lowering creates phi nodes across yield-resume blocks where some incoming values aren't reachable from all predecessors. The codegen needs proper SSA insertion (or a rewrite of the resume-machinery to avoid the dominance violation).

**Fix scope:** large. Requires either reworking the generator state-machine lowering or running mem2reg / a SSA-fixer pass after generator codegen.

**Affected clusters:** `cpn-*-from-yield-expression`, `dstr/array-elem-iter-*-close`, `methods-gen-yield-as-expression-*`, `scope-gen-meth-paramsbody-var-*`.

### 3. Arity mismatch on harness functions (~30 tests)

**Symptom:** `LLVM Module verification failed: Incorrect number of arguments passed to called function!` (e.g., `assertThrowsInstanceOf` called with 3 args, declared with 4).

**Root cause:** the harness function is declared with N parameters but the test calls it with fewer (relying on JS default-undefined). The compiler emits a strict-arity LLVM call.

**Fix scope:** medium. Need to either pad args with `undefined` at call sites, or generate variadic-style trampolines for cross-module function calls.

**Affected clusters:** mostly `staging/sm/TypedArray/*`, `staging/sm/RegExp/*`, scattered in `staging/sm/Set/*`.

### 4. Eval semantics (~16 tests, AOT-incompatible)

**Symptom:** various — block-scoped function declarations in eval-globals.

**Root cause:** runtime `eval` is fundamentally incompatible with AOT. These tests will never pass; consider adding to the skip list with feature `dynamic-import-into-script` or similar.

**Affected cluster:** `annexB/language/eval-code/*`.

### 5. Private-field-in brand check (~11 tests)

**Symptom:** parse error on `#field in obj` or runtime not-implemented.

**Root cause:** ECMA-2022 "ergonomic brand check" feature requires both parser support for `#x in y` and runtime support for private-name `in`-test. Currently unimplemented.

**Affected cluster:** `language/expressions/in/private-field-*`.

### 6. Smaller clusters (~30 tests scattered)

- Unicode 16.0.0 identifier characters (`language/identifiers/start-unicode-16.0.0.js`, etc.) — requires ICU upgrade or per-codepoint table.
- `with` statement (`language/statements/function/S13.2.2_A19_T3.js`, etc.) — `with` is parsed but not lowered. Deprecated; low priority.
- Class-as-function TypeError (`Derived()` without new) — needs runtime TypeError emission for class-symbol-called-without-new.
- Default-constructor symbol issues (`class C extends Base {}` chain) — analyzer/HIR class-extends quirk.
- Codegen missing-block-terminator failures — scattered, each likely a different lowering path.

## Outlook

If the monomorphizer-stub fix lands (option A), ce drops to ~175 (-80). Pass count moves +30 to +50 (most stub-emit tests then fail or pass at runtime depending on what behavior the test expects from the undefined symbol).

If options A + B both land, ce drops to ~140 (-115). Estimated pass gain +60 to +100 (TypeError-on-no-new tests will start passing because the runtime path handles it correctly).

If generator SSA gets a proper fix, ce drops by another ~80. Pass gain there is harder to estimate (many of those tests are negative-tests checking yield semantics that may still fail post-parse).

**Realistic next-quarter target:** ce < 100, pass > 17,400.

## Methodology Notes

The autonomous-loop ce-cluster-fix skill (`.claude/skills/test262-autoloop/SKILL.md`) drove the 27-commit spike. Key lessons captured there:

- **Sweep noise floor is ±6 pass.** Single-commit deltas inside that band are not signal.
- **Trust the ce count, not the pass count, for narrow-fix evaluation** — ce moves are deterministic; pass moves are stochastic from timeout/scheduling effects.
- **Broad TokenKind-based widening is dangerous.** Commit `c666db0` widened the lexer's escapedReservedWord reservation and regressed 170 tests; had to revert (`95569c9`).
- **Spec-citing commits.** Each commit message references the ECMA-262 section being implemented. This discipline rules out cargo-culted "fixes" that match a single engine's quirk rather than the spec.

## Re-Running the Sweep

```bash
python tests/test262/run_test262.py --fresh -j 24 --timeout 8 2>&1 | tail -5
```

Results are written to `tests/test262/.test262_results.jsonl`. The cluster_ce.py helper (committed at `tmp/cluster_ce.py`) tallies ce by 3-segment path prefix and samples top buckets:

```bash
python tmp/cluster_ce.py
```
