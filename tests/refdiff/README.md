# Reference-Diff Harness

Compares ts-aot's output against Node.js for a corpus of small JS scripts.
Any divergence is a candidate bug.

## Why

The spec-invariant probe library (`tests/invariants/`) covers ~40 specific
ECMA-262 rules. The reference-diff harness covers EVERYTHING else: side
effects, edge cases, runtime exceptions, output formatting. If two
spec-conforming engines disagree on `script.js`, one of them (probably us)
has a bug.

This test is **untargeted**: each script is just regular JS, and we let
the diff tell us what's wrong. Run regularly to catch regressions and
discover bugs you didn't think to write probes for.

## Requirements

- Node.js on PATH (run `where node` / `which node` to verify)
- ts-aot built (`cmake --build build --config Release`)

## Corpus rules

`corpus/*.js` scripts must be:

1. **Pure ECMAScript**: no Node-specific APIs (`fs`, `http`, `process.env`,
   `__dirname`, `require()`). Use only what's in ECMA-262.
2. **Deterministic**: no `Date.now`, `Math.random`, `performance.now`,
   `setTimeout`, network I/O. Output must be identical across runs.
3. **Short**: aim for ≤50 lines. The harness times out after 5 seconds
   per script.
4. **Self-asserting OR pure print**: either the script asserts
   internally and prints its results, OR it just prints values that we
   diff. Don't mix random console.log with thrown errors.
5. **Print-friendly**: only print strings, numbers, booleans, and simple
   objects. Avoid `console.log(fn)` since node prints `[Function: name]`
   and ts-aot may print differently.

## Running

```bash
python tests/refdiff/runner.py                 # diff full corpus
python tests/refdiff/runner.py -v              # show matching scripts too
python tests/refdiff/runner.py -k destruct     # filter by name substring
python tests/refdiff/runner.py --bless         # write current ts-aot output
                                               # as the expected baseline
                                               # (use sparingly — only when
                                               #  you've verified the diff)
```

## Layout

```
tests/refdiff/
  README.md
  runner.py
  corpus/
    <name>.js           # the script
    <name>.expected     # node's stdout (auto-generated on first run)
```

The runner caches node's output to `corpus/*.expected` on first encounter
(since node is the reference). Subsequent runs diff ts-aot's output
against the cached file. To refresh node-side baselines, delete the
`.expected` files and re-run.

## Adding scripts

Just drop a .js into `corpus/`. The runner discovers them automatically.
Pick the next available name in the relevant theme prefix (`array_*`,
`class_*`, `closure_*`, `error_*`, `numeric_*`, `string_*`, etc.) or
invent a new prefix.

## Interpreting failures

A failure means ts-aot's stdout differs from node's stdout (or
ts-aot crashed). The runner prints a unified diff. Common patterns:

- `[Function: X]` vs `function X() { [native code] }` — different `Function.prototype.toString` forms; refine the probe to print just `.name` or `typeof`
- ts-aot crash with `FATAL: Uncaught` — a real runtime bug
- Whitespace-only diff — probably a print-format quirk; can be ignored
- Number format diff (`1e+21` vs `1e21`) — `Number.prototype.toString`
  edge case
