---
name: gate-battery
description: The full merge discipline for conformance fixes - branch, probe, triple gates, targeted family diff, merge --no-ff, baseline update, checkpoint. Use before merging ANY runtime/compiler change to master. Trigger terms - "gates", "merge discipline", "ready to merge", "validate the change".
allowed-tools: Bash, Read
---

# The Gate Battery

Every change merges through the same fixed sequence. No step is skippable —
each one has caught a real regression that the others missed. All commands
run from the repo root first (`cd /e/src/github.com/cgrinker/ts-aoc` — cwd
resets between shell calls).

## Sequence

1. **Branch per change**: `git checkout -b <topic>`. Master stays green.
2. **Build ALL targets**: `cmake --build build --config Release`, grep for
   `error C|error LNK`. Then deploy the DLL to BOTH consumers:
   ```
   cp build/src/sharedrt/Release/tsruntime_shared.dll tests/test262/
   cp build/src/sharedrt/Release/tsruntime_shared.dll tmp/
   ```
   A stale DLL in either location produces phantom results.
3. **Probe suite**: re-run the tmp/ probe set for this change AND the
   accumulated probes from prior merges (they are the fast regression
   matrix — see differential-probes).
4. **Triple gates** (each must be clean):
   - node suite: `python tests/run_all.py --suite node` → 297/297
   - golden IR: `python tests/golden_ir/runner.py tests/golden_ir -j 12`
     — if a golden test "regresses" because it encoded OLD broken behavior,
     update its expectation with a comment, don't weaken the fix
   - 2k gate: from tests/test262,
     `TS262_SHARED_RUNTIME=1 python run_test262.py --paths-file
     tmp_gate_paths.txt -j 20 --results-file X.jsonl` then
     `python tmp_gate_diff.py X.jsonl` → 0 lost (3–5 TypedArray --fast
     flakes excepted)
5. **Targeted family diff**: run the affected test family (pass+fail set
   pulled from the current baseline jsonl) and diff vs baseline. Print BOTH
   gained and LOST lists. **0 lost is the merge bar** — losses go through
   regression-triage, not revert-by-reflex and not baseline laundering.
6. **Merge**: commit with EXPLICIT file paths (never `git add .` — the tree
   carries unrelated tracked churn), message = mechanism + probes + family
   counts + known limits, ending with the Co-Authored-By trailer. Then
   `git checkout master && git merge --no-ff <topic> && git branch -d`.
7. **Baseline update**: merge the family-rerun jsonl into a new
   `tmp_pNN.jsonl` (last-wins by path), regenerate `.test262_baseline.json`
   (sorted keys), commit — **`git add tests/test262/.test262_baseline.json`
   FROM THE REPO ROOT** (a relative pathspec from tests/test262 silently
   matches nothing).
8. **Checkpoint memory** (see cycle-checkpoints), then next cycle.

## Environmental Hard Rules

- **NEVER build while a sweep is running** — the runner executes freshly
  compiled exes; a mid-sweep rebuild corrupts thousands of results.
- Sweeps are SEQUENTIAL only; two resume-runners delete each other's exes
  (~1,700 phantom WinError-2 crashes historically).
- `pgrep` does not exist in this git-bash — watchers use
  `tasklist //FI "IMAGENAME eq python.exe"`.
- A full fresh sweep is the only trustworthy topline; per-family baseline
  patchworks accumulate flake-passes and mixed path forms. Bookend long
  runs with full sweeps.

## Net-0 Policy

A change that measures net-0 after triage gets DROPPED — bank the diagnosis
to memory instead. Exception: zero-risk ABI/crash-class corrections (a
registration matching a C signature, a longjmp-safety guard) may merge with
an honest "+0 measured" commit message, because they are prerequisites or
close real-world crash classes even when no test moves.
