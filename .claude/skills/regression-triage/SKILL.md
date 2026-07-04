---
name: regression-triage
description: Recover from test regressions by triaging each loss into exposure/regression/stale-flake and fixing forward, instead of reverting the branch. Use when a family rerun or sweep reports LOST tests after a change. Trigger terms - "regression", "LOST N", "tests lost", "revert or fix".
allowed-tools: Bash, Read, Edit, Write, Grep
---

# Regression Triage: Fix Forward, Don't Reflex-Revert

When a measured run reports lost tests after a change, the change is usually
NOT wrong — the loss set almost always decomposes into distinct classes with
different correct responses. Reverting throws away a good fix to appease a
bad baseline. Triage first.

## The Triage Classes

Classify EVERY lost test into one of:

1. **Honest exposure** — the change unmasked a real pre-existing bug that a
   masking behavior was hiding. Signature: the new failure reason describes
   spec-correct behavior the old code never exercised.
   Example from this repo: the module parse-error containment exposed that
   the export-of-undeclared-name check rejected import bindings
   (`import * as ns; export { ns }`) — masked for months by the node-parser
   fallback silently eating the parse failure.
   → **Fix the exposed bug in the same branch.** The exposure is a gift.

2. **Genuine regression** — the change itself broke the behavior. Signature:
   reverting just the suspect hunk (or gating it) restores the test AND the
   failure mechanism traces to the new code.
   Example: the first draft of generator prototype natives crashed on
   primitive receivers (`this-val-not-object`) because the magic read
   dereferenced a tagged value — fixed by nanbox-validating before the read,
   in-branch, same cycle.
   → **Repair in-branch and re-measure.** Only drop the whole change if the
   repair defeats its purpose.

3. **Stale baseline / flake** — the test was never really passing; the
   baseline recorded a flake or a patchwork artifact. Signature: the test
   ALSO fails on commits that predate your change.
   Example: two TypedArray `speciesctor-get-ctor-abrupt` "regressions" in a
   fresh sweep bisected to failing at merge 104 — three merges before the
   suspected cause. The old per-family baseline had recorded a `--fast`
   batch flake as a pass.
   → **Re-baseline; no code action.** Note it in memory so nobody chases it
   again.

## The Procedure

1. **Isolate**: re-run each lost test alone (`--filter <name> -j 1`) — batch
   flakes disappear here; deterministic failures remain.
2. **Read the reason**, not just the status. "Expected X got Y" tells you
   which class you're in faster than any bisect.
3. **Bisect only when classification is ambiguous**: check out the merge
   BEFORE your suspected window, rebuild, re-run the one test. One build at
   the window's midpoint usually settles it (fails there too → class 3).
4. **Fix in the same branch** for classes 1 and 2, then re-run the FULL
   family diff — the fix must bring losses to 0 without giving back the
   gains.
5. Gate everything again after the repair. The final commit message names
   each loss class and what was done about it (see merge 98's "20 losses:
   12 honest exposures, 4 my regressions, 4 leak-class" breakdown).

## Hard Rules

- **0 lost is the bar for merging** — but "0 lost" is achieved by triage and
  repair, not by discarding the branch at the first red number.
- A change that measures **net-0 after repair** gets dropped (bank the
  diagnosis to memory) — EXCEPT zero-risk ABI/crash-class corrections, which
  may merge with an honest "+0 measured" commit message.
- Never re-save the baseline to hide a class-2 loss. Baselines only absorb
  class-3 findings, and only after the bisect proves them stale.
- When a "regression" appears in a FULL sweep against a patchwork baseline,
  suspect class 3 first: per-family baseline merges record flakes as passes.
