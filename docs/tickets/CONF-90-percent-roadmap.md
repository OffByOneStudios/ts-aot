# CONF-90: Road to 90% test262 conformance

**Created:** 2026-07-07 (autoloop session end)
**Baseline:** 39,394 / 45,333 run = 86.9% (commit 694cc10c) + ~38 merged since
(iter-3/iter-4 defineProperty) ≈ **39,432 effective**
**Target:** 90% of 45,333 = **40,800 pass** → gap ≈ **+1,368**

Skips (5,173: staging/sm, intl402, Atomics/SAB) stay out of the denominator.

## Strategy

Ten tactical phases ordered by yield-per-risk cover ~+1,100 of the gap; the
last ~+270 requires committing to ONE architectural investment (Phase K
options). Every phase follows gate-battery discipline (branch → probe →
golden_ir + node + focused family sweep → merge --no-ff → baseline refresh on
clean full sweeps). Estimates use the session's measured yield pattern
(~30-50% of vein size for narrow fixes; probe-report per-bug numbers where
they exist).

| Phase | Vein | Est. gain | Cumulative (est.) |
|-------|------|-----------|-------------------|
| A | defineProperty bugs 2,3,4,8,5 (accessor model, get-less shadow, array-index descriptors, non-extensible redefine, arguments map) — per-bug coordinates in the 2026-07-06 probe report | +70-80 | 39,510 |
| B | String.prototype 5-bug report: @@replace/@@split/@@match dispatch preambles, replaceAll getSubstitution + functional replaceValue, matchAll non-global TypeError, JS whitespace set for trim*, substring/split index coercion | +100-125 | 39,625 |
| C | RegExp `\d`/`\w`/`\s` → JS ASCII/whitespace classes in the pattern translator (ICU's are Unicode-wide; fixes CharacterClassEscapes + feeds B's trim) + RegExp compile_error tail (named-groups 10, lookBehind 7) | +40-50 | 39,670 |
| D | ICU ≥ 76 bump (Unicode 16 data) — vcpkg dependency change; recovers the ~125 property-escapes honest exposures; regenerate baseline after | +110-125 | 39,790 |
| E | Destructuring iterator protocol (~210 tests: iter-rtrn-close-*, put-targets, trailing-rest across for-of/assignment dstr) — one IteratorClose-on-abrupt subsystem | +80-120 | 39,890 |
| F | Array.prototype generic/array-like receivers (336: concat/splice/push S15.4 families, length-near-2^53 clamping = the 13 timeouts) | +100-150 | 40,010 |
| G | TypedArray BigInt-TA storage root (known deep root in memory; blocks ~339-test family incl. TypedArrayConstructors/internals) | +80-120 | 40,110 |
| H | Class narrow residue: private-brand TypeErrors + shadowed private accessors (263 private union; compound-assignment private-reference LHS rides along) | +100-150 | 40,260 |
| I | Singles harvest: `with` (97), Date.prototype (75), JSON/stringify (45), Object.prototype (75) — grind in 2-3 batches, drop when <8/commit | +120-160 | 40,410 |
| J | dynamic-import namespace (76) + for-await-of (54) | +80-110 | 40,510 |

**Phase K (pick one, ~+270-350):**
1. **Direct-eval caller environment** — functions containing direct `eval`
   compile in env-backed-locals mode (locals in a TsMap the interpreter can
   read/write). Unlocks annexB/language/eval-code (177) + language/eval-code
   residue (37) + declare-arguments RUN variants ≈ +200-250, and improves
   `with`. Design sketch already in EVAL-001 ticket §3.
2. **Class R1/R2 architectural fixes** (source-position computed-key install
   done; remaining calling-convention/install residue across the 500-test
   class family).

**Stability prerequisite (do early, zero test yield):** the R14 callee-saved
register GC-root gap (memory: regexp-charclass-crash-r14-register-root) —
compiled code holding GC pointers in callee-saved registers across minor GC.
It crashes big-loop workloads nondeterministically and will contaminate
phase measurements if left; repro is tmp/probe_cce2.js.

## Measurement discipline

- Full sweep + `--save-baseline` at each phase boundary; expect noise ±6.
- Milestones: **88%** (39,893) ≈ end of Phase E; **89%** (40,346) ≈ end of
  Phase I; **90%** (40,800) requires Phase K.
- Correct this document's estimates with measured deltas as phases land —
  numbers over hope.
