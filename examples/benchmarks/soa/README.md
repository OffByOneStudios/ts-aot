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

## Current result (2026-07-14): fast beats our dynamic path 5.4x; V8 still ahead

Three-way, identical checksums (`nbody_node.js` is the same algorithm in
plain JS, run by node when it's on PATH):

| Contender | wall time | minus startup* | vs fast |
|---|---|---|---|
| fast (ts-aot, NativeArray SoA) | **123 ms** | ~105 ms | — |
| dynamic (ts-aot, number[]) | 1087 ms | ~1069 ms | 8.8x slower |
| node v22 (V8 JIT, plain JS) | 128 ms | ~101 ms | 1.04x slower |

\* startup measured separately: ts-aot empty exe ≈ 18 ms, `node -e 0` ≈ 27 ms.

**The fast path now matches V8** (slightly ahead on wall time, effectively
tied on pure compute) and beats our own dynamic path 8.8x. The journey from
the inverted 2026-07-06 result, each step measured on this benchmark:

| Change | fast time |
|---|---|
| baseline (2026-07-06 write-up) | 2653 ms |
| typed CallMethod results (NativeArray.get element type; Math.* direct typed calls) | 217 ms |
| `Math.sqrt/abs/floor/ceil/trunc` → LLVM intrinsics (fast modules) | 185 ms |
| no GC pin for NativeArray handles (fast modules) + `ts_tdz_sentinel` as inline constant (all modes — a hot loop body paid one runtime CALL per block-scoped declaration per iteration) | 123 ms |

The sentinel fix also sped the dynamic path up (1148 → 1087 ms): every
block-scoped `let`/`const` in every program was re-seeding its TDZ slot
through an out-of-line call at each block entry.

### History: the 2026-07-06 result was inverted (fast ~2.2x SLOWER)

The original suspects (per-access call overhead, GC statepoints, redundant
handle reloads) were all ruled out by differential probes at the time — and
were indeed all wrong. The actual root, found by reading the inner-loop IR:
the HIR result type of `arr.get(j)` (and of `Math.sqrt`) was **Any**, so every
arithmetic op touching those values lowered through the boxed
`ts_value_sub/mul/div` runtime dispatcher — three runtime calls plus two
NaN-boxings *per subtract* — even though the element access itself was already
an inline unboxed load. The loop also paid a dead `ts_get_global_Math()` call
per iteration (~31M calls total) just to evaluate the receiver of `Math.sqrt`.

Fix (both gated on the fast directive, in `ASTToHIR_Expressions_Calls.cpp`):
`NativeArray.get` stamps its unboxed element type on the call result, and a
global-builtin method with a typed RuntimeCall resolution (all of `Math.*`)
lowers to a direct typed call with no receiver evaluation. The inner loop is
now pure native `fmul/fadd/fsub` plus one `ts_math_sqrt(double)` call.

The **dev-mode safety checks** (Phase 3's other half) are complete and working:
build with `--fast-checks` and run with `TS_FAST_CHECKS=1` to get loud
bounds / use-after-dispose / double-dispose aborts; the release default inlines
with no checks.
