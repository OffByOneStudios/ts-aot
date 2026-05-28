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

- **`builtin_props_survive_minor_gc.js`** — THE minimal repro (one forced
  minor GC). `Object.prototype.toString.call([])` returns "[object Array]"
  before a minor GC and `undefined` after: builtin objects lose their
  property-map contents across a minor GC. The toString *function* survives
  and compiler intrinsics still work, so it's the builtin objects' backing
  property storage that the minor GC clobbers — the global object graph is
  reachable only via `extern "C" TsValue* global` (TsObject.cpp:9386), a
  .data-segment pointer the conservative scan doesn't cover, with no
  `ts_gc_register_root`. Sensitive to conservative stack pinning, so the test
  holds no pre-GC result across the GC.

- **`cyclical_clone_deep.js`** — moving-GC corruption on deep recursive
  cloning of cyclic object graphs. Distilled from lodash `test.js`'s
  `_.cloneDeep should deep clone objects with lots of circular references`
  (the test that blocks the lodash upstream harness under default nursery
  at 188 tests / 6.1% executed). Its user-visible symptom — `seen.has(value)`
  mis-dispatching to `ts_set_has_wrapper` ("Set method called on incompatible
  receiver") — is almost certainly the SAME root cause as the builtin_props
  repro: `Map.prototype` (and the global Map binding) corrupted by the minor
  GC the same way `Object.prototype` is, so method resolution on `seen` goes
  wrong. Fix the builtin-graph rooting and both should clear.

  See `docs/tickets/GC-001-verification-harness-and-moving-gc-fix.md` and the
  `gc-001-verification-harness` memory.

## Root cause + fix direction (next session)

The minor GC does not reliably keep the **global builtin object graph** alive
and correctly forwarded. `global` (TsObject.cpp:9386) is a plain
`extern "C" TsValue*` in `.data`; the conservative scanner only scans the
stack, and there is no `ts_gc_register_root(&global)`. Builtin maps (Object,
Object.prototype, Array.prototype, Map.prototype, ...) are created in the
nursery at init and survive only by accident (conservative pinning of
whatever happens to be on the stack at GC time — hence the pinning
sensitivity).

Candidate fixes (in increasing robustness):
  1. `ts_gc_register_root((void**)&global)` after `global` is assigned, so the
     whole reachable builtin graph is a marked root → promoted + fixed up by
     the first minor GC. Smallest change; verify it actually marks through the
     boxed TsValue.
  2. Tenure builtin prototype/constructor maps at init (ts_gc_alloc_old_gen),
     so they never move. More sites, but matches the Phase 3a / BUG 4 pattern.
  3. Both.
Validate against BOTH testbeds here plus the full gc-suite + golden_ir + node.
