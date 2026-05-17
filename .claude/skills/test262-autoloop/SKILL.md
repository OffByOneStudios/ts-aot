---
name: test262-autoloop
description: Run an autonomous loop that iteratively reduces the test262 compile_error OR runtime-fail tails by multi-axis cluster analysis, applying focused fixes, and verifying with regressions+sweep. Use when the user says "autonomous loop", "auto-loop", "ce cluster spike", "fail cluster spike", "multi-loop test262", or invokes `<<autonomous-loop-dynamic>>`.
allowed-tools: Bash, Read, Edit, Write, Grep, Glob, ScheduleWakeup, Agent
---

# test262 Autonomous-Loop Cluster-Fix Skill

Iteratively reduce the test262 `compile_error` (ce) tail by sampling its largest clusters, picking ONE narrow target per iteration, fixing it, verifying with regression suites + a fresh sweep, and chaining iterations via `ScheduleWakeup` until the narrow-target pool exhausts.

## When to Use

**Trigger terms:** autonomous loop, auto-loop, ce cluster spike, fail cluster spike, multi-loop test262, cluster-fix, `<<autonomous-loop-dynamic>>`

Use this skill when:
- The user invokes `<<autonomous-loop-dynamic>>` and wants you to continue picking and fixing test262 tests.
- The user asks to "spike on compile errors" or "drive ce down" or "spike on failures".
- The user explicitly wants a multi-iteration autonomous run (not a single fix).

Do NOT use this skill for:
- A single narrow fix the user has already identified — just fix it directly.
- Adding new features (this skill is regression/conformance-focused).
- Deep architectural work (generator SSA, monomorphizer specialization) — these don't fit the narrow-fix cycle.

## Cluster-Attack Methodology (read first)

This skill works in two modes: **ce mode** (narrow parser/lexer fixes against compile_error tail, ~1-25 tests/commit) and **fail mode** (semantic fixes against runtime-fail tail, ~30-300 tests/commit). Fail mode came on-line 2026-05-17 after a +272-pass class-elements session; methodology rules below apply to both, with fail mode having additional discovery work.

### Rule 1: Multi-axis cluster survey at session start

Before iteration 1, dispatch a research agent to cluster failures along ≥4 orthogonal axes:
- 3-segment path prefix
- Normalized error-message shape
- `esid:` frontmatter
- `features:` frontmatter tag
- Procedural template source

The `features:` tag is the highest-information-density axis (cluster sizes via spec concept, not file path). Path-prefix has higher raw reach but fragments same-root-cause tests. See `runtime-fail-clusters.md` and `session_2026-05-17_class_elements_triple.md` for prior outputs of this survey.

Reuse `tmp/cluster17k/` scripts if present and recent. Skip the survey only if the previous session's survey is <24h old AND results still show the planned cluster.

### Rule 2: Parallel agent probing after each commit

After a commit lands cleanly, dispatch **3 parallel research agents** to probe the next 3 candidate clusters from the survey. Each agent's brief:
- Read 3 random failing tests in your assigned cluster
- Compile each with `build/src/compiler/Release/ts-aot.exe <path> -o tmp/x.exe`, run, capture stderr
- Identify the file:line in `src/` where the divergence likely originates
- Return: bug description (1 paragraph), proposed fix scope (1 paragraph), risk note (1 paragraph)

Use the results to pick which cluster to attack next. The cost is ~1 minute of agent time; the value is parallel discovery that overlaps with build/sweep wait time.

### Rule 3: Probe with test262 tests, not custom code

When investigating a cluster, **first** run a representative test262 file from the cluster directly. Reach for custom probes ONLY if the test262 source is too dense to read. The harness already builds the test262 file (with `propertyHelper.js` etc.) via:

```bash
python -c "
import sys; sys.path.insert(0, 'tests/test262')
from run_test262 import build_test_source, parse_frontmatter
from pathlib import Path
p = Path('<test262-file>'); m = parse_frontmatter(p.read_text(encoding='utf-8'))
Path('tmp/probe.js').write_text(build_test_source(p, m), encoding='utf-8')"
build/src/compiler/Release/ts-aot.exe tmp/probe.js -o tmp/probe.exe 2>&1 | tail -3
tmp/probe.exe 2>&1
```

Custom probes lose 10-15 min and produce divergent results vs the harness. Don't write them by default.

### Rule 4: 1-cluster-3-bugs budget (fail-mode runtime work)

**Empirical pattern, hard-confirmed 2026-05-17:** the `prod-FieldDefinition × Expected SameValue` "single root cause" agent estimate (1,100 tests) decomposed into 3 distinct bugs (method identity, propertyIsEnumerable, delete tombstone). Each bug ~30-100 LOC, each unlocked 50-400 tests.

Rules for budgeting fail-cluster work:
- Survey says "X tests, 1 root cause" → budget **3 commits**, expect ~30% yield per commit
- After fixing the first bug, **always re-probe** the cluster — second/third bugs are hidden behind the first failing assertion
- A single test like `verifyProperty` aborts at the first sub-failure; fixing it surfaces the next layer
- Don't claim "one fix" until 3 representative tests in the cluster pass end-to-end

### Rule 5: No full-sweep between commits

Full sweep (50,506 tests) takes 15-30 min and blocks decision-making. Between commits, trust:
- `golden_ir` regression suite (10s)
- `node` regression suite (15s)
- Focused test262 sweep on the cluster path (`-c language/statements/class/elements -j 12 --timeout 8 --fresh`, 1-2 min)

Run full sweep only at session end OR if a focused sweep shows unexpected behavior outside the cluster path.

### Rule 6: Refresh baseline after clean commits

The runner compares against `.test262_baseline.json`. Stale baselines produce false-regression noise (this confused the class-elements session — 6 "regressions" were tests already failing). After each commit with `0 real regressions AND non-zero new passes`, refresh the baseline with `python tests/test262/run_test262.py --save-baseline`. The autoloop now does this automatically when `--auto-baseline` is passed (added 2026-05-17).

## The Cycle (one iteration)

Each iteration runs these steps. The wakeup → focused-sweep → fix → schedule chain is what makes it "multi-loop". Full corpus sweep only at session start (for baseline) and session end (for measurement).

**Iteration 1 only — Multi-axis cluster survey:** if no recent survey artifact exists (`tmp/cluster17k/` empty or >24h old), dispatch a research agent to cluster the fail+ce tail along path/error/esid/features/template axes. Use the result to pick the first 3 target clusters.

1. **Read the latest sweep results.** Parse `tests/test262/.test262_results.jsonl` and count statuses.
2. **Pick a cluster** from the survey (or cluster path-3 for ce-mode work — `tmp/cluster_ce.py` — see Artifacts below). Prefer `features:` or `esid:` axis clusters over raw path-3 — they capture spec invariants, not file layout.
3. **Probe with test262 tests, not custom code** (Rule 3). Run 3 representative tests through the harness via `build_test_source` + ts-aot + execute. Read the failure messages.
4. **Pick ONE narrow target** that:
   - Has a clear root cause (parser rule missing, codegen primitive-boxing missing, runtime helper wrong, etc.)
   - Lives in a single source file or a tight neighborhood
   - Doesn't widen TokenKind-based reservations beyond their spec-correct scope (see Dangers below)
5. **Implement the fix.** Use Edit. Keep changes ≤30 LOC for ce-mode; ≤150 LOC for fail-mode runtime fixes.
6. **Build:** `cmake --build build --config Release 2>&1 | tail -3` (runtime fixes need full build, not just `--target ts-aot`).
7. **Verify the sample test compiles + runs** (for ce→fail transitions, the test progressing to a runtime error is still progress).
8. **Run regression suites in parallel:** golden_ir + node + a focused test262 sweep on the cluster path. All must show "No regressions."
   - `python tests/golden_ir/runner.py tests/golden_ir 2>&1 | tail -3`
   - `python tests/node/run_tests.py 2>&1 | tail -3`
   - `python tests/test262/run_test262.py -c <cluster-path> -j 12 --timeout 8 --fresh 2>&1 | tail -10`
9. **Commit** with a descriptive message referencing the ECMA-262 section (see Commit Template below).
10. **Re-probe the same cluster** (Rule 4). If 3 random cluster tests still fail with a NEW shape, you've hit the second bug — go to step 4. Three commits per cluster is the empirical mean.
11. **Parallel-probe the next 3 clusters** (Rule 2). Dispatch 3 research agents while the focused sweep runs, each probing one candidate cluster from the survey. Use their reports to pick iteration N+1's target.
12. **Schedule the next iteration** with `ScheduleWakeup(delaySeconds=1500, reason="multi-loop iter N: <commit summary>", prompt="<<autonomous-loop-dynamic>>")`.

**Session end:** run the full sweep, refresh baseline if zero real regressions, write a memory entry summarizing yield + methodology lessons.

When the wakeup fires, the next iteration starts at step 1 with fresh data.

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
# Use --auto-baseline so the baseline JSON refreshes when this sweep is
# clean (0 regressions, non-zero new passes). Stops stale-baseline
# false-regression noise from accumulating across sessions.
python tests/test262/run_test262.py --fresh --auto-baseline -j 24 --timeout 8 2>&1 | tail -5

# 2. ScheduleWakeup:
ScheduleWakeup(
    delaySeconds=1500,
    reason="multi-loop iter N: <commit summary>",
    prompt="<<autonomous-loop-dynamic>>"
)
```

### Focused cluster sweep (between commits, replaces full sweep)
```bash
# Use this between commits to verify a fix and detect cluster-local regressions
# without paying the 30-min full-sweep cost. `<cluster-path>` is the path the
# cluster lives under (e.g., language/statements/class/elements).
python tests/test262/run_test262.py -c <cluster-path> -j 12 --timeout 8 --fresh 2>&1 | tail -10
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

### CE mode (parser/lexer narrow fixes)

A single session ran 26 commits across ~14 autonomous iterations:
- Pass: 17,137 → 17,159 (+22)
- Ce: 384 → 257 (-127, **33% reduction**)
- 0 golden_ir/node regressions on any merged commit
- 1 revert (broad escapedReservedWord widening — the canonical anti-pattern)

Biggest single-commit wins (-12 to -67 ce):
- `64ce932` ComputedPropertyName in ObjectBindingPattern: **-67 ce**
- `1ed98d1` class expression extends parity: **-12 ce**
- `15aeb5c` top-level await tightening: **-8 ce**
- `df536c5` contextual keywords as let-binding identifiers: **-8 ce**

### Fail mode (semantic/runtime fixes, methodology added 2026-05-17)

Single class-elements session, 3 commits:
- Pass: 17,553 → 17,825 (**+272 net** — largest single-session delta in project runtime work)
- Fail: 17,159 → 16,889 (-270)
- 0 golden_ir/node regressions
- Cluster: `prod-FieldDefinition × Expected SameValue` (~1,100-test cluster found via multi-axis survey)
- Discovery pattern: agent estimated "1 root cause" → reality was 3 distinct bugs (method identity / propertyIsEnumerable / delete tombstone). Each bug was 30-100 LOC of runtime code, each unlocked 50-400 tests.

Commits:
- `b19c934` flat-object method identity (constructorSlot back-pointer) + propertyIsEnumerable
- `4247911` NANBOX_DELETED tombstone for inline-slot delete

The pattern: each fix touches a single ECMA-262 invariant the runtime had wrong. Citing the spec section in the commit message is the discipline that prevents broad/wrong "fixes". For fail-mode, **budget 3 commits per "single root cause" cluster** — second/third bugs hide behind the first failing assertion.

## Related Skills

- `auto-debug` — use when a sample test crashes (segfault), not when it parse-errors.
- `golden-ir-tests` — invoke the regression suite (the cycle uses `python tests/golden_ir/runner.py` directly).
