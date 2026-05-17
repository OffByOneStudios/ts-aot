# Spec-Invariant Probes

Standalone `.js` files exercising a single ECMA-262 invariant each. Designed to
catch spec divergences before they show up in the test262 fail tail.

## Why this exists

The 2026-05-17 class-elements session found 3 distinct bugs in the runtime
(method identity, propertyIsEnumerable, delete tombstone) only *because* one
test262 test happened to probe them. With ~17K fail-bucket tests, individual
bugs hide behind each other (first failing assertion aborts the test, hiding
the rest).

This probe library inverts that: one probe per invariant, runs in <5s total,
each failure pinpoints exactly which spec rule is broken.

## Conventions

Every `probes/*.js` file:
1. Has a header comment with the ECMA-262 section citation
2. Sets up a minimal situation
3. Asserts exactly ONE invariant
4. Prints `PASS` if the invariant holds
5. Prints `FAIL: <reason>` otherwise (with enough detail to diagnose)

Probes must NOT depend on each other. Each is run in isolation by the runner.

The runner classifies a probe as:
- `pass`: stdout contains `PASS\n`
- `fail`: stdout contains `FAIL:` (with reason printed)
- `crash`: non-zero exit, no FAIL prefix
- `compile_error`: ts-aot failed to produce an exe

## Running

```bash
python tests/invariants/runner.py                 # run all
python tests/invariants/runner.py -v              # show passes too
python tests/invariants/runner.py -k method_id    # filter by name
```

Or via the unified runner:
```bash
python tests/run_all.py --suite invariants
```

## Adding probes

Pick the next available filename in the relevant category prefix:
- `method_identity_*` — function-object identity through prototype chain
- `descriptor_*` — property descriptor attributes
- `proto_chain_*` — prototype chain shape
- `delete_*` — `[[Delete]]` semantics
- `samevalue_*` — `===` and Object.is edge cases
- `array_exotic_*` — Array-specific behaviors (length, holes)
- `symbol_*` — Symbol semantics
- `fn_metadata_*` — Function .name / .length
- `accessor_*` — Getter / setter dispatch
- `iterator_*` — Iterator protocol

Keep each probe ≤30 lines. If you need more setup, split into multiple probes.
