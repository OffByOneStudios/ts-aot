# Garbage Collector Roadmap: From Boehm to Precise Collection

## Current State: Boehm GC

### How We Allocate Today

Two-tier allocation via `ts_alloc()` (src/runtime/src/Memory.cpp):

| Size | Strategy | Mechanism |
|------|----------|-----------|
| <= 512 bytes | Pool allocator | Thread-local blocks (256KB) per size class (8, 16, 24, ..., 512). Bump-pointer within blocks. Blocks allocated with `GC_malloc_uncollectable()` so GC scans but never frees them. |
| > 512 bytes | Direct GC | `GC_malloc(size)` for normal collection |

**Boehm APIs used:**
- `GC_INIT()` - Initialization in `ts_main()`
- `GC_malloc()` - Large object allocation
- `GC_malloc_uncollectable()` - Pool block allocation
- `GC_set_max_heap_size(2GB)` - Heap limit
- `GC_gcollect()` - Forced collection (exposed as `ts_gc_collect()`)
- `GC_set_abort_func()` - Custom error handler

No explicit roots, no finalizers, no thread registration. Boehm scans the stack conservatively.

### What Gets Allocated (All via `ts_alloc`)

| Object | Size | Allocation Rate |
|--------|------|-----------------|
| TsValue (boxing) | 16 bytes | Massive - every primitive crossing any/typed boundaries |
| TsArray | ~48 bytes + 8B/element | Per array |
| TsMap (object literals) | ~56 bytes + hash table | Per object literal |
| TsString | ~64 bytes (small) | Per string |
| TsClosure | ~64 bytes + 8B/capture | Per closure |
| TsFunction | ~40 bytes | Per function reference |

### Problems with Boehm

| Problem | Severity | Impact |
|---------|----------|--------|
| False retention (integers look like pointers) | High | Memory bloat, unpredictable OOM |
| Pool blocks never freed | High | Monotonic memory growth for small objects |
| "Too many heap sections" | High | Crashes on large workloads (gc_stress benchmark) |
| STW pauses scale with heap size | Medium | Latency spikes |
| No compaction | Medium | Heap fragmentation |
| WeakRef is fake (strong reference) | Medium | Incorrect JS semantics |
| FinalizationRegistry unreliable | Medium | Incorrect JS semantics |
| Boxing pressure causes OOM | High | fibonacci benchmark crash |

---

## LLVM GC Statepoints Infrastructure

### Core Concept

LLVM statepoints replace normal function calls with instrumented calls that record which GC pointers are live across the call. This enables precise stack scanning at GC time.

### Three Intrinsics

```llvm
; 1. gc.statepoint - wraps a call, recording live GC pointers
%tok = call token @llvm.experimental.gc.statepoint.p0(
    i64 <id>, i32 0, ptr @callee,
    i32 <nargs>, i32 0, <call args...>
) [ "deopt"(), "gc-live"(ptr addrspace(1) %obj1, ptr addrspace(1) %obj2) ]

; 2. gc.result - extracts the call's return value
%ret = call ptr addrspace(1) @llvm.experimental.gc.result.p1(token %tok)

; 3. gc.relocate - gets the (potentially moved) pointer after GC
%obj1.new = call ptr addrspace(1) @llvm.experimental.gc.relocate.p1(
    token %tok, i32 0, i32 0)
```

### RewriteStatepointsForGC (RS4GC) Pass

The key insight: you don't emit statepoints manually. Instead:

1. Emit normal LLVM IR with `ptr addrspace(1)` for GC pointers
2. Attach `"deopt"()` operand bundles to every call
3. Set `gc "ts-aot-gc"` on every function
4. RS4GC automatically:
   - Computes GC pointer liveness at every call
   - Wraps calls in `gc.statepoint`
   - Inserts `gc.relocate` for every live pointer
   - Rewrites all downstream uses

### Address Space Convention

- `ptr` (addrspace 0) = non-GC pointers (function pointers, libuv handles, C strings)
- `ptr addrspace(1)` = GC-managed pointers (TsObject*, TsArray*, TsString*, TsValue*)

### Custom GC Strategy

```cpp
class TsAotGC : public llvm::GCStrategy {
public:
    TsAotGC() {
        UseStatepoints = true;
        UseRS4GC = true;
    }
    std::optional<bool> isGCManagedPointer(const Type *Ty) const override {
        if (auto *PT = dyn_cast<PointerType>(Ty))
            return PT->getAddressSpace() == 1;
        return std::nullopt;
    }
};
static GCRegistry::Add<TsAotGC> X("ts-aot-gc", "ts-aot GC strategy");
```

### Stack Maps

The backend emits `.llvm_stackmaps` section containing:
- Per function: address, stack size, record count
- Per safepoint: instruction offset, location entries
- Per location: kind (Register/Indirect/Direct), register number, offset

At runtime, parse this to build a `return_address -> root_locations` lookup table.

### Key Gotchas (LLVM 18)

1. Still "experimental" prefix but stable (Julia uses in production)
2. No automatic safepoint polling in loops - must insert manually if needed
3. Windows COFF: section name truncated to 8 chars
4. SEH interaction complex with invoke statepoints
5. For non-moving GC: `gc.relocate` returns same pointer (LLVM optimizes away)

---

## Migration Phases

### Phase 1: Precise Non-Moving Mark-Sweep (Foundation)

**Effort: 4-8 weeks**

**Compiler changes (~60% of work):**
1. Register custom GC strategy (`TsAotGC`)
2. Add `gc "ts-aot-gc"` attribute to all generated functions
3. Use `ptr addrspace(1)` for GC-managed pointer types throughout HIRToLLVM.cpp
4. Attach `"deopt"()` bundles to every `CreateCall`
5. Add `RewriteStatepointsForGC` to the pass pipeline
6. Raw C pointers (libuv, OpenSSL) remain in addrspace(0)

**Runtime changes (~40% of work):**
1. Free-list allocator with size classes (replace Boehm + pool allocator)
2. Per-type GC descriptors using existing magic numbers
3. Stack map parser for `.llvm_stackmaps` section
4. Precise marking starting from stack roots + globals
5. Sweeping with free list reclamation
6. Proper WeakRef with GC-integrated weak pointers
7. Proper FinalizationRegistry with deterministic callbacks

**Fixes:** False retention, WeakRef/FinalizationRegistry semantics, pool allocator memory leak, "too many heap sections"

### Phase 2: Generational (Nursery)

**Effort: 3-5 weeks**

1. 4MB bump-pointer nursery
2. Semi-space copy of survivors to old gen
3. Card-marking write barriers in compiler (every pointer store)
4. Barrier elision for stores to newly-allocated objects

**Impact:** 10-100x allocation speed. Sub-ms nursery pauses. Fixes fibonacci crash.

### Phase 3: Concurrent Old-Gen Marking

**Effort: 3-5 weeks**

1. Background marking thread
2. SATB write barrier during marking phases
3. Two short STW pauses (<1ms)

**Impact:** Old-gen pauses <1ms regardless of heap size.

### Phase 4: NaN-Boxing (Orthogonal)

**Effort: 2-4 weeks**

Eliminate TsValue* allocation entirely. Every JS value fits in 64 bits:
```
Double:    any valid IEEE 754 double
Pointer:   0x0000_XXXX_XXXX_XXXX (48-bit, top bits 0)
Integer:   0xFFF8_0000_XXXX_XXXX (32-bit int in bottom bits)
Boolean:   0x7FF9_0000_0000_000X
Null:      0x7FFA_0000_0000_0000
Undefined: 0x7FFB_0000_0000_0000
```

**Impact:** Eliminates the single largest source of GC pressure.

### Gradual Migration Strategy

You don't have to replace Boehm all at once:

1. **Step 1**: Add statepoints + stack map generation, keep Boehm. Verify maps are correct.
2. **Step 2**: Precise roots + Boehm heap (hybrid). Better than fully conservative.
3. **Step 3**: Replace Boehm allocator with custom free-list. Full precision.
4. **Step 4**: Add nursery + write barriers.
5. **Step 5**: Add concurrent marking.

Each step is independently testable and provides incremental improvement.

---

## Key References

| Resource | Relevance |
|----------|-----------|
| [Whippet GC](https://github.com/wingo/whippet) | Embeddable GC library (semi-space, Immix, generational) |
| [MMTk](https://www.mmtk.io/) | GC framework in Rust; bindings for V8, Julia, Ruby |
| Go's `runtime/mgc*.go` | Concurrent mark-sweep with precise stack scanning |
| GraalVM Native Image | AOT compiler with precise generational GC |
| Julia's GC | Most mature LLVM statepoint user |
| Blackburn & McKinley, "Immix" (PLDI 2008) | Mark-region collector design |

---

## Appendix: GC Comparison Table

| Design | Examples | Allocation | Pause Time | Fragmentation | Complexity |
|--------|----------|------------|------------|---------------|------------|
| Conservative mark-sweep | Boehm | Free-list (slow) | O(heap) STW | Yes | Low |
| Precise mark-sweep | Go (early), Phase 1 | Free-list | O(heap) STW | Yes | Medium |
| Generational + mark-sweep | OCaml, .NET Gen0/1 | Bump-pointer (fast) | O(nursery live) | Partial | Medium-High |
| Concurrent mark-sweep | Go (current) | Free-list | <1ms STW | Yes | High |
| Generational + concurrent | V8, JSC, .NET | Bump-pointer + concurrent | <1ms | Partial | Very High |
| Region-based (Immix) | MMTk | Bump-pointer | O(region live) | Minimal | High |
