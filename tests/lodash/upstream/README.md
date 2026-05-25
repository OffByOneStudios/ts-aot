# Lodash official test suite under ts-aot

Goal: make ts-aot compile + run lodash 4.17.21's **own** QUnit test suite
(`test/test.js`, ~27k lines, **6794 assertions**) — the rigorous bar for
"lodash works", beyond our hand-written `tests/lodash/*.ts`.

## Why this layout

- `test.js` is **not** on the npm registry (the published tarball strips
  `test/`). It only exists in the lodash git repo. We fetch it from the
  `4.17.21` tag on demand — **not vendored**.
- QUnit comes from npm (`qunit`) but test.js actually uses a tiny surface
  (`QUnit.module/test`, `assert.strictEqual/deepEqual/ok/...`). We ship a
  minimal **`qunit_shim.js`** instead of dragging in real QUnit's
  DOM/async/HTML-reporter machinery.

## Files

| File | Committed? | Purpose |
|------|-----------|---------|
| `qunit_shim.js` | yes | Minimal QUnit + assert, tallies pass/fail, supports async via libuv drain |
| `harness.js` | yes | Entry: pre-sets `global.QUnit/_/lodashStable`, requires test.js, runs queue |
| `setup.py` | yes | Fetches `test.js` (git tag) + copies `lodash.js`; both gitignored |
| `test.js` | no | lodash 4.17.21 `test/test.js` (fetched) |
| `lodash.js` | no | the 4.17.21 bundle under test (copied from `../lodash.js`) |

## Run

```bash
python tests/lodash/upstream/setup.py        # fetch test.js + lodash.js

# Node reference (validates the shim): expect ~6790/6794
node tests/lodash/upstream/harness.js

# ts-aot (the actual target):
build/src/compiler/Release/ts-aot.exe tests/lodash/upstream/harness.js -o tmp/lu.exe
tmp/lu.exe                                    # prints LODASH-QUNIT PASS/FAIL/TOTAL
```

## Status (2026-05-25)

- **Node baseline**: 6794 assertions, 0 fail (via qunit-extras).
- **Shim fidelity under Node**: 6790 pass / 4 fail (99.94%) — the 4 are
  exotic deepEqual/async edge cases in the shim, not lodash bugs.
- **ts-aot**: the full 44k-line harness now **COMPILES** (after fixing the
  `.push()` static-misdispatch for Object + Any receivers — commits `3aaa0a9`
  and the Any follow-up). It **runs into lodash init and crashes** on the
  next bug: cross-module `global.X` doesn't round-trip (a `global.foo` set in
  one module reads `undefined` in a `require()`d module), so test.js's
  `root.lodashStable` is undefined → `interopRequire(...).noConflict()` →
  TypeError. Repro `tmp/xmod/`. See memory `[[lodash-upstream-testjs-harness]]`.

## AOT-incompatible tests (expected irreducible SKIPs)

`_.template`/`attempt` via eval, `runInContext` vm realms, exact
`debounce`/`throttle` timing, DOM-node cloning. These are gated in test.js
behind environment probes (`document`, `vm`, `phantom`) that resolve absent
under ts-aot, so most degrade gracefully.
