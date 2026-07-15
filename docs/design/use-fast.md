# RFC: `use fast` — a High-Performance TypeScript Subset for ts-aot

**Status:** Draft / RFC
**Date:** 2026-07-05
**Owners:** (tbd)
**Related:** `src/compiler/analysis/Analyzer_Escape.cpp`, `src/compiler/hir/HIRToLLVM_Memory.cpp`, `Type.h` (`TypeKind::Int`, `ClassType::isStruct`)

---

## 1. Summary

`use fast` is an opt-in, **enforced** TypeScript dialect for the hot 5% of a
program. Inside a "fast" file the compiler *rejects* any construct that would
force boxing, an implicit heap allocation, dynamic property access, or a
megamorphic dispatch. In exchange it guarantees native-quality codegen:
unboxed values, fixed-shape structs with static field offsets, arena/stack
memory instead of the GC, no write barriers, and SIMD-friendly data layout.

The mental model is **Unity's Burst / HPC# / DOTS**, *not* Rust. We buy
predictable performance and memory safety with a **subset + arena lifetimes +
dev-only runtime checks**, not a compile-time borrow checker. That is an order
of magnitude less compiler work and is proven on shipping game code.

The deliverable is a *contract*: "the compiler proves there are no hidden
allocations, no boxing, and no dynamic dispatch in this scope — or it won't
compile." Predictability is the product.

---

## 2. Motivation

AOT-compiling TypeScript is slow in the same places every JS engine is slow,
except we pay at compile-authored-time rather than at JIT-warmup:

- **Boxing/unboxing.** `any` and dynamic paths traffic in `TsValue`.
- **GC allocation.** Every object/array/closure hits the generational GC
  (`TsGC.cpp`): block allocator, nursery + old gen, card-table write barriers,
  precise root pushing via LLVM stack maps.
- **Dynamic property access.** Fields live in a `TsMap` (hash lookup +
  prototype walk) unless the shape is statically known.
- **Dynamic dispatch.** Method calls on `any` go through vtables / name-based
  dispatch.

For most code this is fine — it's what makes the language dynamic. For a
physics step, a particle system, an image filter, or an ECS tick, it is fatal.
`use fast` is the escape hatch: a place where the type system is strong enough,
and the language small enough, that the existing optimizer *always* fires.

### The key insight

The compiler already *sometimes* does the right thing (see §4). The problem is
it's best-effort: a single `any`, a dynamic key, or an escaping closure silently
drops you onto the slow path with no diagnostic. `use fast` turns
"sometimes-optimized" into "always-optimized-or-compile-error."

---

## 3. Non-goals

- Not a general speedup for ordinary TS. Dynamic code stays dynamic.
- Not a new language. Valid fast code is valid TypeScript — a **strict subset**,
  never a superset. (This is the #1 lesson from AssemblyScript's "looks like TS
  but isn't" UX problem.)
- Not (for v1) a compile-time borrow checker or lifetime system.
- Not a concurrency/data-race model (v1). That's where a Unity-Job-style
  dependency graph would come later.

---

## 4. What ts-aot already has (this is ~40% built)

`use fast` is mostly about *promoting existing best-effort optimizations to
enforced guarantees*, plus a type-system dialect on top.

| Capability | Where | State |
|---|---|---|
| Escape analysis (`escapes` per allocation) | `Analyzer_Escape.cpp` | advisory |
| Stack allocation of non-escaping flat objects | `HIRToLLVM_Memory.cpp` | capped at **4 objects / 512 bytes**, and **disabled under `--gc-statepoints`** |
| Struct value types | `ClassDeclaration.isStruct` → `ClassType.isStruct` | partial |
| Integer type distinct from float | `TypeKind::Int` (i64) vs `Number` (f64) | present |
| Monomorphization of generics | Monomorphizer | present |
| Native/contiguous storage substrate | TypedArrays (`Int32Array`, `Float64Array`, …) | present |
| Native codegen + SIMD | LLVM 18 backend | present |
| Precise GC roots | LLVM stack maps | present |

### The architectural tension we exploit

`HIRToLLVM_Memory.cpp:448` documents that **stack allocation and precise GC
statepoints are mutually exclusive**: addrspace(0) stack pointers cannot mix
with addrspace(1) GC pointers, so today the compiler chooses one globally, and
stack-alloc is *off* whenever precise GC is *on*.

A "fast" file declared **no-GC** dissolves this conflict. That single property
cascades:

- unconditional stack/arena allocation (drop the 4/512 cap)
- **no write barriers** (the card-table barrier on every field store vanishes)
- **no statepoints / no root spilling** at call sites
- no boxing → no `TsValue` traffic

This is the difference between "a bit faster" and "C-competitive," and it is
*uniquely* available to us because we own the GC and the backend.

---

## 5. Prior art

- **V8 Torque** — typed DSL for V8 builtins; the crucial idea is a type-level
  **tagged (GC pointer) vs untagged (raw machine value)** distinction, with a
  *guaranteed* lowering. We express the same split as a *region* (fast file)
  rather than threading it through every type.
- **Unity Burst / HPC# / DOTS** — the primary model (see §6). A struct-only C#
  subset, unmanaged `NativeArray` containers with explicit allocators, and
  runtime safety checks in dev builds. No static borrow checker.
- **Microsoft Static TypeScript** (micro:bit) — a TS subset compiled to native;
  direct proof that a restricted TS subset compiles to fast machine code.
- **AssemblyScript** — strict TS subset → WASM with `i32/usize`/linear memory;
  closest sibling, and the cautionary tale on the "two-language" problem.
- **Cyclone / MLKit regions** — region-based memory management; the theory
  behind arena lifetimes without per-object lifetime variables.
- **Rust** — `Copy` vs move, `&`/`&mut`; the thing we deliberately *don't* fully
  adopt in v1.

---

## 6. The Unity mapping

Unity does not use a compile-time borrow checker. It buys memory safety with
four cooperating pieces, each of which maps onto ts-aot:

| Unity | Role | ts-aot equivalent |
|---|---|---|
| **HPC#** (High-Performance C#) | struct-only subset: no managed refs, no GC, no `try/catch`, no virtuals | the **`FastCheck` analyzer pass** — rejects disallowed constructs |
| **Burst** | compiles HPC# → SIMD native | the **LLVM 18 backend** (already there) |
| **NativeArray / NativeContainer + `Allocator`** (Temp / TempJob / Persistent) | unmanaged memory with explicit *region lifetimes*, off the GC heap | **arena/native containers** on the TypedArray substrate |
| **AtomicSafetyHandle** | bounds / dispose / race checks **in dev builds, compiled out in release** | **dev-mode runtime checks** — the cheap alternative to a borrow checker |
| **Job system** (read/write dependency graph) | data-race safety at *schedule* time, not compile time | *future work* (concurrency) |

Takeaway: **subset + arena lifetimes + dev-only runtime checks** ≫
compile-time borrow proof, for a fraction of the effort, on proven ground.

---

## 7. Selection model — "tsconfig + linter over specific files"

Granularity mirrors Unity's **asmdef** (you mark an assembly, not lines):

- **`tsconfig.fast.json`** (or a `"fast": { "include": ["src/sim/**"] }` block in
  the main config) selects which files are compiled under the fast contract.
- The **"linter" is the compiler's own analyzer.** It already infers types; we
  add a `FastCheck` pass that emits errors for anything outside the subset. No
  separate ESLint plugin — the diagnostics come from the place that already
  knows the types, and they can be as precise as "this `obj[key]` forces a hash
  lookup; fast code must use a fixed struct field or a `NativeArray` index."
- Per-file `// @fast` marker as the fine-grained override; per-function
  `"use fast"` prologue as an even finer valve (mirrors `"use strict"`, which
  the parser already stamps as `Program::isStrict`).

`use fast` implies `use strict`.

Recommended default granularity: **whole-file / whole-module**, so the
dynamic↔fast boundary (§12) stays coarse and doesn't box-thrash.

---

## 8. The subset — what `FastCheck` enforces

The subset is defined by "forbid everything that defeats unboxing,
monomorphization, or fixed-shape access." Concretely, in a fast scope:

**Banned (compile error, with a "why + escape hatch" message):**
- `any`, `unknown` (without full narrowing), `object`, index signatures
- reference **classes** (heap objects); only `struct` value types (§9)
- dynamic property access `obj[expr]`, `delete`, adding/removing props post
  construction, prototype mutation
- `eval`, `with`, `arguments`
- `async` / generators (they require a heap suspension frame)
- **closures that capture** (use context structs instead — §11)
- managed allocation: `new C()` of a reference class, object/array literals that
  heap-allocate — memory must come from a `NativeArray`/arena (§10)
- non-discriminated unions (v1); discriminated unions are allowed and compile to
  tagged structs (Rust enums — keep these)
- exceptions across the fast boundary (v1); `try/catch` limited or banned like
  Burst

**Required:**
- concrete monomorphic types everywhere
- fixed-width numerics in hot positions: `i8..i64`, `u8..u64`, `f32`, `f64`,
  `usize` (infer from `x | 0` → i32, TypedArray element types, literals)
- explicit `struct` for value types you want stack/arena-allocated

The linter's error messages are half the product. Each rejection should name
the slow mechanism it prevents and the fast alternative.

---

## 9. Type-system additions

Building on `TypeKind::Int` and `ClassType::isStruct`:

- **Fixed-width numerics**: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 usize isize`.
  Plain `number` is `f64`; in the subset, hot integer positions require an
  integer type. Inference sources: `| 0`, typed-array element type, literal
  suffixes (`1i32`), and flow.
- **`struct` value types** (extend `isStruct`): fixed layout, `Copy` or move
  semantics, stack/arena-allocatable, **static field offsets** (no `TsMap`, no
  hash, no prototype walk). Access lowers to a GEP.
- **Fixed arrays & slices**: `[T; N]` (inline, blittable) and `&[T]` (a
  pointer+length view over a `NativeArray`).
- **`NativeArray<T>`**: the unmanaged, contiguous container (§10). `T` must be
  blittable (struct of primitives).
- **Discriminated unions → tagged structs.** A `{ kind: 'a', ... } | { kind:
  'b', ... }` compiles to a tag + union payload — a Rust enum. Fast and allowed.
- **`Box<T>` / `Rc<T>`**: explicit opt-in heap for the rare case a fast value
  must escape. This is the `unsafe`/valve.

---

## 10. Memory model — allocators, not GC

Fast files are **no-GC**. Memory comes from explicit allocators (Unity's model),
expressed as familiar handles rather than lifetime syntax:

```ts
// src/sim/physics.ts  — a "fast" file

struct Vec3 { x: f32; y: f32; z: f32; }

function step(pos: NativeArray<Vec3>, vel: NativeArray<Vec3>, dt: f32): void {
  for (let i: i32 = 0; i < pos.length; i++) {
    pos[i] = { x: pos[i].x + vel[i].x * dt,
               y: pos[i].y + vel[i].y * dt,
               z: pos[i].z + vel[i].z * dt };   // no boxing, no GC, SIMD-able
  }
}

// caller
const scratch = new NativeArray<Vec3>(n, Allocator.Temp);  // bump-alloc arena
// ... use ...
// freed automatically at scope end (Temp) — no GC, no barriers, no statepoints
```

Allocators:
- **`Allocator.Temp`** — frame/scope arena; bump-allocated, bulk-freed at scope
  end. This is the region model (Cyclone/MLKit) with a Unity name.
- **`Allocator.Persistent`** — long-lived, manual `dispose()`, off the GC heap.
- (`TempJob` reserved for the future job system.)

Because a fast file is no-GC, `HIRToLLVM_Memory.cpp` can stack/arena-allocate
unconditionally (drop the 4-object/512-byte cap), elide the card-table write
barrier on field stores, and skip statepoints/root-spilling entirely (§4).

---

## 11. No closures → context structs (dissolving the borrow problem)

The hardest case for stack allocation and the thing that pushes people toward a
borrow checker is **a closure capturing stack data and escaping.** Unity's
answer is to *not have closures*: a Job is a `struct` whose fields are its data,
with an `Execute()` method. State is carried explicitly.

We adopt the same rule. Instead of:

```ts
const total = arr.reduce((a, x) => a + x * scale, 0);  // captures `scale`
```

you write a context struct:

```ts
struct SumScaled { scale: f32; }
// method carries data explicitly; nothing is captured
function run(self: SumScaled, arr: NativeArray<f32>): f32 {
  let a: f32 = 0;
  for (let i: i32 = 0; i < arr.length; i++) a += arr[i] * self.scale;
  return a;
}
```

This one rule eliminates the entire aliasing/lifetime headache: nothing is
captured, so there is nothing to reason about. It is worth more than a borrow
checker and costs nothing to enforce.

---

## 12. Safety model (v1): subset + arenas + dev-mode runtime checks

No static borrow checker in v1. Memory safety comes from three layers, matching
Unity:

1. **Subset restriction** — no managed refs, no dynamic escape hatches; most
   unsafe patterns are simply not expressible.
2. **Arena lifetimes** — `Allocator.Temp` frees at scope end; a value tagged
   with an arena may not escape it (a *scope* check, not a lifetime-variable
   check). This is the only static "ownership-ish" rule, and it's cheap.
3. **Dev-mode runtime checks (AtomicSafetyHandle analog)** — in debug builds,
   `NativeArray` access does bounds + "disposed?" + optional alias/race checks;
   in release these compile out. Fast-and-safe in dev, fast in release.

A full borrow checker (moves, `&`/`&mut`, aliasing-XOR-mutability) is deferred
to the concurrency story, if and when a job scheduler needs static read/write
disjointness. Even then, Unity got a long way with a *schedule-time* dependency
graph rather than a compile-time proof.

---

## 13. The real game lever: data-oriented layout

The biggest DOTS win is not stack-vs-heap for one object — it's
**Structure-of-Arrays**: contiguous, cache-friendly, SIMD-friendly component
storage. `NativeArray<struct>` over the TypedArray substrate is exactly the
vehicle. The subset should make SoA the natural default:

- iterate `NativeArray<Vec3>` linearly → the vectorizer and prefetcher win
- prefer parallel component arrays (`NativeArray<Position>`,
  `NativeArray<Velocity>`) over arrays-of-heap-objects
- this is where 10–50× lives, far more than a single `alloca`

The `FastCheck`/codegen should be biased toward, and eventually able to
diagnose, layout that defeats vectorization.

---

## 14. Interop boundary (fast ↔ dynamic)

Fast code and normal TS interoperate through an explicit, **coarse** seam — like
FFI:

- calling *into* fast code from dynamic code: arguments are unboxed at the seam;
  a `NativeArray` handle crosses as a pointer+length.
- calling *out* to dynamic code from fast code: values are boxed at the seam;
  doing this in a hot loop defeats the purpose, so `FastCheck` should warn on
  dynamic calls inside fast loops.
- keep the boundary at module granularity to avoid box-thrash.

A `Box<T>` is the explicit valve for a fast value that must live on the GC heap.

---

## 15. Codegen mapping (how it flows through the existing pipeline)

1. **Parser** stamps `fastScope` on functions/files from the directive / config
   (same mechanism as `Program::isStrict`).
2. **Analyzer / `FastCheck`** runs the subset check and the stricter,
   escape-*enforcing* type inference. Errors here; nothing reaches HIR that
   violates the contract.
3. **HIR**: fast allocations carry `escapes=false` by construction (or are a
   compile error); `struct` types get fixed layouts; `NativeArray` ops lower to
   pointer+index.
4. **HIRToLLVM**: fast values → unboxed LLVM types (`i32/i64/double`, `struct`
   by value); struct field access → `GEP` (no `TsMap`); method calls →
   direct/monomorphized calls; allocations → `alloca`/arena bump; **no write
   barriers, no statepoints** in the no-GC region.
5. **Backend**: normal LLVM optimization + vectorization; SoA loops vectorize.

The seam (§14) is the only place boxing/roots reappear.

---

## 15b. Implementation status (2026-07-06)

- **Phase 0 — DONE** (`0022e6d5`). `"use fast"` prologue directive → `Program::isFast`
  (implies strict); `Analyzer_FastCheck` pass rejects the out-of-subset
  constructs (any-annotations, delete, with, for-in, for-await-of, eval,
  arguments, async/await, generators/yield, dynamic import) with actionable
  diagnostics. Inert without the directive; gated (node 300/300, golden, 2k).
- **Phase 1a — DONE** (`366b47a5`). Fixed-width numeric aliases
  `i8..i64 / u8..u64 / usize / isize / f32 / f64` recognized in `parseType`,
  mapped to `Int`/`Double` with `numericBits` + `numericUnsigned` metadata on
  `Type`. Compile via the existing unboxed paths today; width-honoring codegen
  is a follow-up.
- **Phase 1b — IN PROGRESS.**
  - `struct Foo {}` syntax — **DONE** (`37110393`): contextual keyword via a
    one-token parser lookahead buffer; sets `ClassDeclaration::isStruct`.
  - Fixed-width **field/param/var** types — **DONE** (`4e2aa0f2`):
    `convertTypeFromString` maps the aliases so struct/class fields typed
    i32/f64 get unboxed Int64/Float64 slots (were falling to a boxed Any slot
    that silently dropped stores). Struct value types now work end-to-end in a
    `use fast` file: fixed-shape unboxed fields, static-offset access, and
    stack-alloc when non-escaping (via the existing escape analysis).
  - Value/Copy semantics — **DONE** (`81f20a3a`): structs are true value types.
    Assignment, binding, argument passing, and return COPY the instance via a
    runtime `ts_flat_object_clone` inserted by `maybeCloneStruct` (only for
    struct lvalue reads into a value context; a fresh new/call is not cloned).
    Test `tests/fast/test_struct_value_semantics.ts` covers all four sites.
  - Also a permanent test suite — **DONE** (`2024c4d3`): `tests/fast/` (13 tests)
    wired into `run_all.py` as the `fast` suite; positive (numerics, struct
    fields/params/value-semantics) + negative (each FastCheck rejection) + a
    non-fast gating test.
  - Still pending (refinements, not blocking): exact machine widths (i32
    wraparound / real f32 — HIR has only Int64/Float64); nested-struct DEEP copy
    (clone is shallow — correct for primitive fields); guaranteed inline/arena
    storage (that's Phase 2's no-GC region).

  **Phase 1b is functionally complete**: `struct` value types with fixed-width
  unboxed fields, static-offset access, methods/params/returns, true value
  semantics, and stack-alloc when non-escaping — all behind `use fast`.
- **Remaining FastCheck rules — DONE (2026-07-14)** (were deferred until the
  alternatives existed): reject reference-class `new` (structs + NativeArray
  stay allowed; diagnostic points at both), managed array/object literals
  (→ NativeArray / struct+new), and capturing closures (scope-stack detection:
  an identifier in a nested function naming an ENCLOSING function's local;
  sibling nested-function CALLS are exempt — statically resolved, no heap
  cell; module-level bindings are globals, not captures; → context structs).
  Tests: test_reject_ref_class_new / _managed_literals / _capture.ts.
  Still open from the original list: non-discriminated unions (type-level,
  needs the analyzer's union model).

### Phase 2 status (2026-07-06)

- **2a — NativeArray runtime container — DONE** (`15c5e6c6`). Unmanaged
  (malloc-backed, off-GC), contiguous, typed. C ABI in
  `src/runtime/src/TsNativeArray.cpp`: `ts_native_array_new(length, allocKind)`,
  `_length`, `_get_f64/_set_f64`, `_get_i64/_set_i64`, `_dispose`. 8-byte slots
  for now (HIR Int64/Float64 widths). Inert (no compiler wiring), so 0
  regression.
- **2b — compiler integration — DONE** (`2b19a2c2`). `NativeArray<T>` is usable
  end-to-end in fast `.ts`, all gated on the directive. (1) analyzer registers
  the `NativeArray` ClassType + ctor + `Allocator{Temp,Persistent}` only when
  fastFile_ (`Analyzer_Core.cpp`) — undefined outside fast files; (2)
  `NativeArrayHandler` lowers `.get(i)`/`.set(i,v)`/`.dispose()` to the runtime,
  picking `_f64` vs `_i64` from the element type carried on the receiver
  HIRType; (3) `new NativeArray<T>(n[, Allocator.X])` -> `ts_native_array_new`
  (ASTToHIR + an explicit i64,i64->ptr signature in `HIRToLLVM_Calls.cpp` — the
  generic all-ptr fallback would mislink the int args); (4) `.length` ->
  `ts_native_array_length` (`HIRToLLVM_Memory.cpp`, mirroring String.length).
  Test `tests/fast/test_native_array.ts`. Gates: node 300/300, golden no-reg,
  fast 14/14, 2k 0 lost/0 gained.
  **`arr[i]` indexing sugar — DONE (2026-07-14).** The deferred gotcha (the
  className matching `.find("Array")` routed `[i]` to ts_array_get/set and
  corrupted) is defused: lowerGetElem/lowerSetElem now have first-class
  NativeArray branches (inline unboxed slot access; the checked runtime call
  under `--fast-checks` — bounds/dispose diagnostics work through the sugar),
  and createGetElem infers the unboxed element type from the receiver so
  downstream arithmetic stays native (the SoA-benchmark re-boxing trap).
  Test `tests/fast/test_native_array_index.ts` (f64 + i64, both forms
  interoperating, loop pattern).
- **2c — Temp arena — DONE** (`a75a5500`). **Measure-first finding that
  reframed 2c:** the original scope (flip statepoints / lift the stack-alloc cap
  / drop write barriers) delivers almost nothing. A non-escaping local struct is
  already **scalar-replaced to registers** by existing escape-analysis + SROA in
  *both* GC modes (probed: 0 heap, 9 `sr.` allocas) — SROA beats stack-alloc for
  the common case, so the cap/statepoint flip is moot. And write barriers can't
  be dropped for the managed strings/objects fast code still allocates without
  breaking the generational GC. The one genuine, self-contained, GC-safe win is
  the arena, so 2c is scoped to it:
  - Runtime (`TsNativeArray.cpp`): thread-local chain of 1 MiB bump blocks +
    LIFO frame markers, off the GC heap. `Allocator.Temp` bump-allocates and
    `dispose()` is a no-op; `Allocator.Persistent` stays malloc/free.
    `ts_native_arena_mark()`/`_release(token)` — depth-token+resize is robust to
    a throw's longjmp bypassing a release (an outer release reclaims it).
  - Compiler (gated on entry `program->isFast` -> `fastModule_`): mark at each
    fast function's entry, release on every return / fall-through (no-op for
    non-fast/async/gen, so non-fast codegen is byte-identical — verified 0 arena
    calls in non-fast IR).
  - Semantics (Unity Allocator.Temp): returning a Temp array past its frame is
    UB; loop-body Temp allocs accumulate until the frame returns (use a helper
    call per iteration, or Persistent+dispose).
  - Probes tmp/na1.ts (4 mark/4 release balanced) + tmp/arena_stress.ts (200k
    Temp calls + 100k Persistent churn, bounded) -> tests/fast/test_arena.ts.
    Gates: node 300/300, golden no-reg, fast 15/15, 2k 0 lost/0 gained.
  - **Deferred (were the original 2c):** auto-selecting the conservative-GC
    (`--no-gc-statepoints`) path + lifting the stack-alloc cap for whole-program
    fast builds — low value given SROA, revisit only if profiling of a real
    kernel shows non-SROA flat-object heap traffic dominating.

### Phase 3 status (2026-07-06)

- **Dev-mode safety checks — DONE.** The Unity AtomicSafetyHandle analog for
  `NativeArray`, in two coupled halves:
  - Runtime (`TsNativeArray.cpp`): `resolve()` + `in_bounds()` gate every
    access. `TS_FAST_CHECKS=1` (env) turns an out-of-bounds index, a
    use-after-dispose, or a double-dispose into a located `[use fast]`
    diagnostic + `abort()`; default is the silent safe guard. In checks mode,
    `dispose()` of a Persistent array marks it `NARR_DISPOSED` and does **not**
    free (so use-after-dispose is caught instead of reading freed memory — an
    intentional dev leak, like Unity's leak detector); release frees as before.
  - Compiler: `--fast-checks` (`Driver`/`main.cpp` -> `HIRToLLVM::fastChecks_`)
    selects **which lowering** `.get`/`.set` get. **Release default = inline**
    unboxed load/store (`base + 16 + i·8`, raw addrspace-0 handle so it's off
    the GC statepoint path); **`--fast-checks` = the checked runtime call.**
    This is the RFC's "checks compiled out in release." `.length`/`.dispose`
    stay calls (cold). Verified: default IR has 0 element calls (inline),
    non-fast IR unchanged. Probes tmp/oob.ts + tmp/uad.ts (both silent by
    default; loud abort under `--fast-checks` + `TS_FAST_CHECKS=1`).
- **SoA benchmark — RESOLVED 2026-07-14: fast wins 5.28x** (fast ≈217 ms,
  dynamic ≈1147 ms, identical checksum). The 2026-07-06 result was inverted
  (fast ~2.2x slower); the IR profile found the real root — none of the
  original suspects. `arr.get(j)` and `Math.sqrt` carried HIR result type
  **Any**, so every downstream arithmetic op lowered through the boxed
  `ts_value_*` dispatcher (3 runtime calls + 2 NaN-boxes per op), and each
  `Math.sqrt` evaluated its receiver as a dead `ts_get_global_Math()` call.
  Fix (fast-gated, `ASTToHIR_Expressions_Calls.cpp`): `NativeArray.get`
  stamps its unboxed element type; global builtins with typed RuntimeCall
  resolutions lower to direct typed calls. Inner loop is now native
  fmul/fadd/fsub + one `ts_math_sqrt(double)`. Lesson recorded: in a fast
  file, ANY value whose HIR type degrades to Any silently re-boxes all
  arithmetic it touches — future fast-mode features must stamp precise HIR
  types on every producer.
- Gates for all Phase 3 changes: node 300/300, golden 267/279 no-reg, fast
  15/15, 2k 0 lost / 0 gained (the dynamic path is untouched — every change is
  gated on a `NativeArray` receiver or the fast directive).

## 16. Phased plan

**Phase 0 — the contract (no codegen change).**
- `tsconfig.fast` file selection + `// @fast` marker + parser flag.
- `FastCheck` analyzer pass with high-quality diagnostics (name the slow
  mechanism + the fast alternative).
- Deliverable: you can *write* a fast file and watch it reject the wrong code.
  This alone is worth shipping — it's a "will this be fast?" oracle.

**Phase 1 — struct value types + static-offset access.**
- Extend `isStruct` to full value-type semantics; fixed layouts; GEP access.
- Fixed-width numeric types + inference.

**Phase 2 — no-GC regions + arena allocator.**
- Declare fast files no-GC; lift the stack-alloc cap; elide barriers/statepoints.
- `NativeArray<T>` + `Allocator.Temp/Persistent`; scope-escape check.

**Phase 3 — dev-mode safety + SoA benchmark.**
- AtomicSafetyHandle-style dev checks (bounds/dispose), compiled out in release.
- A particle/n-body/mandelbrot benchmark showing the delta vs the dynamic path.
  This is the artifact that sells the feature.

**Phase 4+ (maybe) — jobs/concurrency, then (only if needed) static borrows.**

---

## 17. Open questions / risks

- **Two-language problem.** Enforce "strict subset, never superset." A fast
  file must be compilable (if slower) as ordinary TS with the fast rules off.
- **Boundary box-thrash.** Keep the seam coarse; warn on dynamic calls in fast
  loops. Where is the boundary drawn — module, file, or function?
- **Type-system surface.** i32/u64/&[T]/Box/struct is a real extension. How much
  can we lean on branded types / `satisfies` / existing `isStruct` to bootstrap
  before adding syntax?
- **Closures.** The "no closures, use context structs" rule is ergonomically
  heavy for some code. Is a *non-capturing* closure (compiles to a bare function
  pointer) an acceptable middle ground?
- **Error handling.** Burst bans most `try/catch`. What's our story — result
  types? a `panic` that aborts the region?
- **Arena escape rule.** Scope-based escape is simple but conservative. Is that
  enough, or do a handful of patterns force a lightweight lifetime tag?
- **How strict is `number`?** Ban it in fast files, or silently treat as `f64`
  and only require integer types where indexing/bitops demand it?

---

## 18. Worked example — a particle system

```ts
// src/sim/particles.ts   — "fast" file (tsconfig.fast includes src/sim/**)

struct Particle { x: f32; y: f32; vx: f32; vy: f32; life: f32; }

struct SimConfig { gravity: f32; drag: f32; dt: f32; }

// context struct instead of a capturing closure
function stepAll(cfg: SimConfig, p: NativeArray<Particle>): void {
  for (let i: i32 = 0; i < p.length; i++) {
    const q = p[i];                       // Copy of a value struct
    if (q.life <= 0) continue;
    p[i] = {
      x: q.x + q.vx * cfg.dt,
      y: q.y + q.vy * cfg.dt,
      vx: q.vx * cfg.drag,
      vy: q.vy * cfg.drag + cfg.gravity * cfg.dt,
      life: q.life - cfg.dt,
    };
  }
}

// entry called from ordinary (dynamic) TS — seam boxes/unboxes here only
export function tick(count: i32): void {
  const p = new NativeArray<Particle>(count, Allocator.Persistent);
  const cfg: SimConfig = { gravity: -9.8, drag: 0.99, dt: 1 / 60 };
  stepAll(cfg, p);   // no boxing, no GC, no barriers, vectorizable
  // p persists across frames; dispose on teardown
}
```

`FastCheck` would reject, inside this file: `any`, `p.forEach(x => ...)`
(capturing closure), `p["x"]` (dynamic key), `new Array()` / `[]` (managed
alloc), `async`, adding a field to `Particle` after construction.

---

## 19. Appendix — one-line comparison

| Axis | Rust | Unity DOTS | **ts-aot `use fast`** |
|---|---|---|---|
| Safety mechanism | static borrow checker | subset + arenas + dev runtime checks | **same as Unity** |
| Memory | ownership/lifetimes | `NativeArray` + `Allocator` | **`NativeArray` + `Allocator`** |
| Closures | allowed (with borrows) | none (Job structs) | **none (context structs)** |
| Concurrency | `Send`/`Sync` + borrows | Job dependency graph | future |
| Backend | LLVM | Burst (LLVM) | **LLVM 18 (already here)** |
| Adoption cost | high | medium | **medium, but ~40% pre-built** |
