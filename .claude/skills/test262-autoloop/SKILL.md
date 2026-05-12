---
name: test262-autoloop
description: Run an autonomous loop that iteratively reduces the test262 compile_error tail by cluster-sampling failures, applying focused parser/codegen fixes, and verifying with regressions+sweep. Use when the user says "autonomous loop", "auto-loop", "ce cluster spike", "multi-loop test262", or invokes `<<autonomous-loop-dynamic>>`.
allowed-tools: Bash, Read, Edit, Write, Grep, Glob, ScheduleWakeup
---

# test262 Autonomous-Loop Cluster-Fix Skill

Iteratively reduce the test262 `compile_error` (ce) tail by sampling its largest clusters, picking ONE narrow target per iteration, fixing it, verifying with regression suites + a fresh sweep, and chaining iterations via `ScheduleWakeup` until the narrow-target pool exhausts.

## When to Use

**Trigger terms:** autonomous loop, auto-loop, ce cluster spike, multi-loop test262, cluster-fix, `<<autonomous-loop-dynamic>>`

Use this skill when:
- The user invokes `<<autonomous-loop-dynamic>>` and wants you to continue picking and fixing test262 ce tests.
- The user asks to "spike on compile errors" or "drive ce down".
- The user explicitly wants a multi-iteration autonomous run (not a single fix).

Do NOT use this skill for:
- A single narrow fix the user has already identified — just fix it directly.
- Adding new features (this skill is regression/conformance-focused).
- Deep architectural work (generator SSA, monomorphizer specialization) — these don't fit the narrow-fix cycle.

## The Cycle (one iteration)

Each iteration runs these steps. The wakeup → sweep → fix → schedule chain is what makes it "multi-loop".

1. **Read the latest sweep results.** Parse `tests/test262/.test262_results.jsonl` and count statuses.
2. **Cluster the ce tail by 3-segment path prefix** (`tmp/cluster_ce.py` — see Artifacts below).
3. **Sample 5–15 random ce tests** to identify shared root causes.
4. **Compile a sampled test directly** with `build/src/compiler/Release/ts-aot.exe <path> -o tmp/x.exe 2>&1 | tail -3`. Read the error message.
5. **Pick ONE narrow target** that:
   - Has a clear root cause (parser rule missing, codegen primitive-boxing missing, etc.)
   - Lives in a single source file or a tight neighborhood
   - Doesn't widen TokenKind-based reservations beyond their spec-correct scope (see Dangers below)
6. **Implement the fix.** Use Edit. Keep changes ≤30 LOC where possible.
7. **Build:** `cmake --build build --config Release --target ts-aot 2>&1 | tail -3`
8. **Verify the sample test compiles + runs** (don't worry if it now `fail`s at runtime — ce→fail is still progress).
9. **Run regression suites:** golden_ir + node tests must show "No regressions."
   - `python tests/golden_ir/runner.py tests/golden_ir 2>&1 | tail -3`
   - `python tests/node/run_tests.py 2>&1 | tail -3`
10. **Commit** with a descriptive message referencing the ECMA-262 section if applicable (see Commit Template below).
11. **Start a fresh sweep in the background:**
    ```bash
    python tests/test262/run_test262.py --fresh -j 24 --timeout 8 2>&1 | tail -5
    ```
    Pass `run_in_background=true`.
12. **Schedule the next iteration** with `ScheduleWakeup(delaySeconds=1500, reason="multi-loop iter N: <commit summary>", prompt="<<autonomous-loop-dynamic>>")`.

When the sweep + wakeup fire, the next iteration starts at step 1 with fresh data.

## Sweep Noise Floor (CRITICAL)

**Same-commit re-runs swing pass count by ±6.** Confirmed in iteration 7 of one run: identical commit produced pass deltas of +3 and -6 across consecutive sweeps.

Implications:
- A single-commit pass delta in [-6, +6] is noise, not regression.
- Trust the **ce** count more than the **pass** count for narrow-fix evaluation — ce moves are usually deterministic.
- If you see a -10+ pass swing, suspect a real regression: read the runner's "Regressions (N):" list before committing further.
- Run a diagnostic re-sweep on the SAME commit if you're unsure whether a delta is real.

## Cluster Sampling

The dispatch heuristic is the largest clusters by 3-segment path prefix:

| Cluster | Typical Cause | Fix Scope |
|---------|---------------|-----------|
| `language/expressions/object` (CPN) | Computed property name parsing | Parser (narrow) |
| `language/statements/class` | Class member, extends, FRW | Parser (narrow to medium) |
| `language/expressions/in` | private-field `#x in y` brand check | Feature (heavy) |
| `language/statements/for-of` (dstr) | Destructuring elem init | Parser (narrow) |
| `staging/sm/expressions` | SM-only patterns | Mixed |
| `annexB/language/eval-code` | Runtime eval | **Skip — AOT incompatible** |

After 3 random samples in a cluster, you usually see the same error shape. If the samples diverge wildly, the cluster is heterogeneous — pick a different cluster or sub-bucket.

## Dangers (Don't Do These)

### 1. Broad TokenKind-based widening

Widening `escapedReservedWord` (or any global "is this token an identifier?" check) bypasses context-sensitive reservations like:

- `let` reserved in strict mode only
- `yield` reserved in strict mode OR generator only
- `await` reserved in async function OR module only
- `static`/`implements`/`interface`/`public`/`private`/`protected`/`package` reserved in strict mode only

A precedent: commit `c666db0` widened the lexer's "all keyword-via-escape" rejection to "only true-reserved-via-escape" — this regressed **170 tests** because the parser's downstream KW_xxx checks for strict/async/generator contexts were bypassed for tokens that had been emitted as plain Identifier. Had to revert (`95569c9`) and re-apply only for `async` (`fff37bf`).

**Safe pattern:** if a token is context-sensitive, KEEP it as the specific KW_xxx and add a context check (e.g., `inAsync_`, `strictMode_`) at the parsing site that needs to allow it.

### 2. Touching context-sensitive checks without all 3 sites

Many parser flags (e.g., let-decl lookahead) have 3+ sites:
- `parseStatementOnly`'s let-decl lookahead
- `parseDeclarationOrStatement`'s let-decl lookahead  
- `parseForStatement`'s let-vs-IdentifierRef probe

When fixing one, grep for the others and apply consistently. Miss one and you'll get half-fixed behavior that's worse than the original.

### 3. Spec-incorrect "fixes" that cargo-cult other compilers

V8/SpiderMonkey have quirks. Always cite the ECMA-262 section in the commit message. If you can't cite a spec line, you probably haven't proven the fix is correct.

## Halt Conditions

The loop should terminate (don't schedule another wakeup) when:

1. **Narrow target pool is exhausted.** 3+ consecutive cluster samples return only heavy issues:
   - Generator/yield SSA dominance (`Instruction does not dominate all uses!`)
   - Monomorphizer missing-specialization (`undefined symbol: foo_any`)
   - Codegen verifier failures (`Basic Block does not have terminator!`)
   - Linker undefined harness symbols (`$DETACHBUFFER_any`, `asyncTest_any`)
   - `language/types/undefined` etc. linker errors related to missing builtins
2. **Sweep noise dominates wins.** Two consecutive iterations both fall within ±6 pass and ±3 ce.
3. **Same fix pattern was tried recently.** Don't loop on similar near-misses.

Halt cleanly: don't call `ScheduleWakeup`. Summarize cumulative deltas, list the heavy issues blocking further progress, and stop.

## Commit Template

```
<file>: <one-line summary>

ECMA-262 <section>: <quoted spec rule>. Our parser/codegen <what it did
wrong>. So <minimal test case> failed at <stage>.

<What the fix does in 1-3 sentences.>

Fixes <test path> (exit=0).  
0 golden_ir/node regressions.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

## Quick Commands

### Read latest sweep counts
```bash
python -c "
import json
counts = {}
with open('tests/test262/.test262_results.jsonl','rb') as f:
    for raw in f:
        try:
            r = json.loads(raw.decode('utf-8','ignore'))
            counts[r.get('status','other')] = counts.get(r.get('status','other'),0)+1
        except: pass
print(counts)"
```

### Cluster ce by 3-segment prefix
```bash
python tmp/cluster_ce.py    # see Artifacts
```

### Random sample 10 ce tests
```bash
python -c "
import json, random
ces = []
with open('tests/test262/.test262_results.jsonl','rb') as f:
    for raw in f:
        try:
            r = json.loads(raw.decode('utf-8','ignore'))
            if r.get('status') == 'compile_error':
                ces.append(r.get('path','').replace(chr(92),'/'))
        except: pass
random.seed(42)
for p in random.sample(ces, 10): print(p)"
```

### Probe a sample test
```bash
build/src/compiler/Release/ts-aot.exe <path> -o tmp/x.exe 2>&1 | tail -3
```

### Start background sweep + schedule next iteration
```python
# 1. Bash tool with run_in_background=true:
python tests/test262/run_test262.py --fresh -j 24 --timeout 8 2>&1 | tail -5

# 2. ScheduleWakeup:
ScheduleWakeup(
    delaySeconds=1500,
    reason="multi-loop iter N: <commit summary>",
    prompt="<<autonomous-loop-dynamic>>"
)
```

## Artifacts

### `tmp/cluster_ce.py`

```python
import json, collections

ce_paths = []
with open('tests/test262/.test262_results.jsonl', 'rb') as f:
    for raw in f:
        try:
            r = json.loads(raw.decode('utf-8', 'ignore'))
            if r.get('status') == 'compile_error':
                ce_paths.append(r.get('path', ''))
        except Exception:
            pass

print('total ce:', len(ce_paths))

clusters = collections.Counter()
for p in ce_paths:
    parts = p.replace(chr(92), '/').split('/')
    key = '/'.join(parts[:3]) if len(parts) >= 3 else p
    clusters[key] += 1

print('top 25 clusters:')
for k, v in clusters.most_common(25):
    print('  {:4d}  {}'.format(v, k))

print()
print('samples from top 8 clusters:')
top8 = [k for k, _ in clusters.most_common(8)]
for cluster in top8:
    print('--- ' + cluster + ' ---')
    n = 0
    for p in ce_paths:
        if '/'.join(p.replace(chr(92), '/').split('/')[:3]) == cluster:
            print('  ' + p)
            n += 1
            if n >= 3:
                break
```

Note: `chr(92)` instead of `'\\'` avoids escape issues when this script is generated from a bash heredoc.

## Track Record

A single session of this skill ran 26 commits across ~14 autonomous iterations:
- Pass: 17,137 → 17,159 (+22)
- Ce: 384 → 257 (-127, **33% reduction**)
- 0 golden_ir/node regressions on any merged commit
- 1 revert (broad escapedReservedWord widening — the canonical anti-pattern)

Biggest single-commit wins (-12 to -67 ce):
- `64ce932` ComputedPropertyName in ObjectBindingPattern: **-67 ce**
- `1ed98d1` class expression extends parity: **-12 ce**
- `15aeb5c` top-level await tightening: **-8 ce**
- `df536c5` contextual keywords as let-binding identifiers: **-8 ce**
- `392e949` Intl prototype.constructor backref: **+9 pass**
- `a269591` function param flags scoping: **+13 pass**

The pattern: each fix touches a single ECMA-262 rule that the parser/codegen had wrong. Citing the spec section in the commit message is the discipline that prevents broad/wrong "fixes".

## Related Skills

- `auto-debug` — use when a sample test crashes (segfault), not when it parse-errors.
- `golden-ir-tests` — invoke the regression suite (the cycle uses `python tests/golden_ir/runner.py` directly).
