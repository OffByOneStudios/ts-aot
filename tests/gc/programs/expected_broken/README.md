# Expected-broken GC programs

Programs in this directory are **known unfixed** GC bugs distilled into
minimal repros. They are the testbed for an in-progress GC fix — not a
regression signal. The main `runner.py` invocation skips them by default
(suite stays green); pass `--include-broken` to run them.

```bash
python tests/gc/runner.py                  # default: skips this dir, suite green
python tests/gc/runner.py --include-broken  # also runs these; they WILL fail
```

When a fix lands and a program here starts passing in all configs, move
it back into `tests/gc/programs/` so the runner enforces it as a
non-regression guarantee.

## Current contents

- **`cyclical_clone_deep.js`** — moving-GC corruption on deep recursive
  cloning of cyclic object graphs. Distilled from lodash `test.js`'s
  `_.cloneDeep should deep clone objects with lots of circular references`
  (the test that blocks the lodash upstream harness under default nursery
  at 188 tests / 6.1% executed). See
  `docs/tickets/GC-001-verification-harness-and-moving-gc-fix.md` and the
  `gc-001-verification-harness` memory.
