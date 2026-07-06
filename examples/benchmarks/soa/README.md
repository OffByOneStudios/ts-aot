# SoA n-body benchmark ("use fast" Phase 3)

Quantifies the `NativeArray` Structure-of-Arrays path against the dynamic
`number[]` path on an O(N²) n-body force loop (N=1024, 30 steps). Both files
run the identical algorithm and print the identical checksum; `run.py` compiles
both, verifies the checksums match, and times them.

```
python examples/benchmarks/soa/run.py --runs 3
```

- `nbody_fast.ts` — `"use fast"`, seven `NativeArray<number>` component arrays
  (SoA), inline unboxed loads/stores.
- `nbody_dynamic.ts` — the same math over dynamic `number[]` arrays.

## Current result (honest, 2026-07-06)

On this workload the fast path is currently **~2.2x SLOWER** than the dynamic
path (fast ≈ 2400 ms, dynamic ≈ 1080 ms, ratio ≈ 0.45x). This is a real,
reproducible measurement, and the benchmark exists precisely to surface it.

What we ruled out with differential probes:
- **Not the per-access runtime call.** Inlining `.get`/`.set` to a raw
  load/store (base + 16 + i·8) moved it only 2615 → 2407 ms.
- **Not GC statepoints.** `--no-gc-statepoints` on the fast build was no faster
  (2557 ms).

So the dynamic typed-`number[]` path is already very well optimized (unboxed
contiguous doubles with a specialized fast representation), and today's
`NativeArray` inline access does not yet beat it. The remaining suspects — not
yet isolated — are redundant handle reloads per access (each `arr.get(j)`
re-derives the base pointer), missed vectorization (the inner loop's `Math.sqrt`
is a call/safepoint), and index-conversion overhead. **This is the open
follow-up:** profile the inner-loop IR of both paths and close the gap before
claiming a SoA win.

The **dev-mode safety checks** (Phase 3's other half) are complete and working:
build with `--fast-checks` and run with `TS_FAST_CHECKS=1` to get loud
bounds / use-after-dispose / double-dispose aborts; the release default inlines
with no checks.
