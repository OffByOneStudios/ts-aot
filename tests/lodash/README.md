# Lodash Test Suite

Tests for the full lodash 4.17.21 bundle (`lodash.js`, ~544KB, ~17K lines)
running on the ts-aot compiler.

Each `*.ts` file in this directory:

1. `require('./lodash.js')` — loads the bundle and exercises the runtime end-to-end.
2. Runs a sequence of `assert(expr, label)` checks for one lodash category.
3. Prints `OK: <category>` and `exit 0` on success.
4. On any failure prints `FAIL: <label>` and `exit 1`.

## Run

```bash
python tests/lodash/run_lodash_tests.py            # full suite
python tests/lodash/run_lodash_tests.py -v         # verbose (show all output)
python tests/lodash/run_lodash_tests.py --pattern lang.ts   # one file
```

## Add a new category

Copy an existing file (e.g. `lang.ts`) and replace its tests. Each
assertion gets a label that's printed on failure for quick triage.

## Why a separate suite (not under `tests/node/`)?

Lodash exercises broad combinations of JS features (closures, regex,
prototype chains, function aliases, computed properties) and is the
single largest piece of real-world JS we run end-to-end. Keeping it
isolated lets us iterate on lodash regressions without rebuilding the
much larger Node.js API suite.
