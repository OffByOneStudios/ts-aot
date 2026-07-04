---
name: measure-first-drill
description: Choose conformance work by clustering failures by REASON, picking the biggest coherent group, and reproducing one test before touching code. Includes the low-rate anomaly heuristic, the harness-include check, and stale-diagnosis re-verification. Use at the start of every fix cycle or when deciding what to work on next. Trigger terms - "what next", "cluster", "pick a vein", "drill".
allowed-tools: Bash, Read, Grep
---

# Measure-First Drilling

Never start from a plan's description of a problem. Start from a fresh
measurement, because the codebase moves and diagnoses go stale. The cycle:
cluster → pick → reproduce ONE → only then read code.

## Cluster by REASON, Not by Directory

Directory counts ("Array/prototype: 347") hide structure. Group failing
tests by the first ~60 chars of their failure reason:

```python
c = collections.Counter(); samples = collections.defaultdict(list)
for r in results:
    if r['status'] == 'fail' and TARGET in path(r):
        key = str(r.get('reason',''))[:60]
        c[key] += 1; samples[key].append(shortname(r))
```

A reason-group is a candidate ROOT; a directory is just a location. The
same reason across many methods (e.g. "Expected a TypeError" across
pop/push/shift/unshift) is ONE rule to implement, not 16 bugs. Conversely,
a big directory that fragments into 2-test reason-groups is a vein to skip.

**Enumerate before committing**: a 34-count reason bucket can be one root or
a garbage-bin of generic assertion text. Count the distinct FILES/methods
inside it before believing the number.

## Selection Heuristics (ranked by observed payoff)

1. **Rate anomalies**: a small builtin group with a bizarrely LOW pass rate
   (parseFloat at 13.6%) usually means the builtin is completely dead
   through ONE call path while another path (globalThis.X) works and masks
   it. One fix, whole group. Found +42 in one cycle.
2. **Harness includes first**: before drilling a "feature" cluster, check
   whether the tests' shared `includes:` helper even RUNS. One unsupported
   idiom in a harness file kills entire families — the resizable-ArrayBuffer
   family (+84) died on a single `new Function('return class MyX extends X {}')`
   in resizableArrayBufferUtils.js, not on any missing feature.
3. **Crashes over failures**: within a cluster, a crash (0xc0000005 /
   3221225477) is a cleaner root than a wrong-value assert — one crash often
   takes out a whole sub-family.
4. **Coherent ≥8**: if the biggest actionable group in a vein is under ~8
   tests, the vein is dry — move to the next queue item rather than chasing
   singles.

## Reproduce ONE Before Coding

Run a single representative test in isolation with the AUTHORITATIVE runner
(`TS262_SHARED_RUNTIME=1 python run_test262.py --filter <name> -j 1` from
tests/test262 — standalone tmp probes link a stale static runtime and can
mislead). Read the test's actual assertion and its fixture files. The test
often acquires the feature through a path you didn't expect (e.g. intrinsics
via `Object.getPrototypeOf(fn).prototype`, not via the instance chain).

## Re-Verify Banked Diagnoses

Before starting any task described by a plan, memory note, or old task list:
spend two minutes probing whether the diagnosis still holds. Two full
planned workstreams this project evaporated on contact:
- "bigint-TA storage root (~330)" — probing showed exact 2^53+1 round-trips;
  an earlier tier had already fixed it. The 145 remaining failures were
  unrelated protocol residue.
- "[[Call]]/[[Construct]] split" — all core behaviors already passed;
  intervening merges had fixed it as a side effect.

A stale diagnosis costs a probe to detect and a whole session to chase.
When one is found stale, CORRECT the memory note immediately so no future
session re-inherits it.
