# ARCH: Unified Property Access Protocol

## Problem

ts-aot's property access is implemented as a 600-line `ts_object_get_property` function with separate branches for every object type (flat objects, TsMap, TsArray, TsString, TsFunction, TsClosure, native C++ objects, side-map fallback). Each branch has its own prototype chain handling (or lack thereof), its own getter/setter dispatch (or lack thereof), and its own fallback behavior.

This means every new feature (property attributes, Object.create descriptors, setPrototypeOf) must be implemented correctly in EVERY branch independently. Real-world code like Express fails because it creates objects that cross branch boundaries — a native TsServerResponse with a TsMap prototype chain containing a TsClosure value, resolved through a side-map with property attributes.

The system works in isolation but immediately breaks when real-world code combines features.

## Current State

### Runtime Property GET paths (TsObject.cpp:1992-2624)

| Object Type | Prototype Chain | Getters | Property Attributes | Object.prototype |
|-------------|:-:|:-:|:-:|:-:|
| Flat objects | NO | NO | NO | Partial |
| TsMap | YES | YES | YES (new) | YES |
| TsArray | NO | NO | NO | Hardcoded methods |
| TsString | NO | NO | NO | Hardcoded methods |
| TsFunction | Properties map only | NO | NO | call/apply/bind |
| TsClosure | YES (new via Fix #4) | YES (new) | YES (new) | call/apply/bind |
| Native C++ | Virtual dispatch | NO | NO | NO |
| Side-map fallback | YES (new via Fix #4) | YES | NO | NO |

### Runtime Property SET paths (TsObject.cpp:5647-5806)

| Object Type | Setter Chain Walk | __proto__ | Writable Check |
|-------------|:-:|:-:|:-:|
| Flat objects | NO | NO | NO |
| TsMap | YES (new) | YES | NO |
| TsClosure | NO | NO | NO |
| Native C++ | vtable dispatch only | NO | NO |

### Compiler Property Access paths (HIRToLLVM.cpp)

| Path | Getters | Setters | Prototype Chain | When Used |
|------|:-:|:-:|:-:|-----------|
| SROA (scalar replaced) | NO | NO | NO | Stack-allocated objects with known shape |
| Flat object fast path | NO | NO | NO | Objects with compile-time known shape |
| Shape-cached access | NO | NO | NO | Objects with inferred shape |
| Runtime fallback | Via runtime | Via runtime | Via runtime | Everything else |

## Proposed Architecture

Inspired by V8's `LookupIterator` and JSC's `getOwnPropertySlot` pattern:

### Core Principle: Separate "own property resolution" from "chain walking"

Every object type implements a small interface for resolving OWN properties. A single, shared function walks the prototype chain and calls the resolver at each level.

### Layer 1: Property Resolver Interface

```cpp
// Every object type provides these — via vtable, function pointers, or inline
struct PropertyResolver {
    // Returns PROPERTY_NOT_FOUND sentinel if not found
    TsValue* (*getOwn)(void* obj, const char* key);
    bool     (*setOwn)(void* obj, const char* key, TsValue* value, uint8_t* attrs);
    bool     (*hasOwn)(void* obj, const char* key);
    void*    (*getPrototype)(void* obj);
};
```

Each object type has a resolver:
- **Flat objects**: Check inline slots → check overflow map
- **TsMap**: Check hash table (with attributes)
- **Native C++ objects**: Virtual dispatch → check side-map
- **TsClosure**: Check properties TsMap
- **TsArray/TsString**: Check for known method names → return native function wrappers

### Layer 2: Unified Chain Walker

```cpp
TsValue* ts_property_get(void* receiver, const char* key) {
    void* current = receiver;
    while (current) {
        PropertyResolver* resolver = get_resolver(current);
        
        // Check for getter first
        TsValue* getter = resolver->getOwn(current, __getter_key);
        if (getter != NOT_FOUND) {
            return invoke_getter(getter, receiver);  // 'this' is receiver, not current
        }
        
        // Check for data property
        TsValue* value = resolver->getOwn(current, key);
        if (value != NOT_FOUND) return value;
        
        // Walk chain
        current = resolver->getPrototype(current);
    }
    return undefined;
}
```

Key: the chain walker handles getters, prototype walking, and Object.prototype fallback. Individual resolvers only handle own properties.

### Layer 3: Compiler Fast Paths (Unchanged)

The compiler's typed paths remain exactly as they are:
- **Known class fields**: Direct GEP + load (no runtime dispatch)
- **Known flat object shapes**: Inline slot access
- **SROA**: Per-property allocas

These bypass the runtime entirely when types are known at compile time. The unified protocol only governs the **slow/dynamic path** — when the compiler can't determine the type.

### How to Implement the Resolver Dispatch

Two options:

**Option A: Magic-number dispatch table**
Use the existing magic numbers (offset 0 or 16) to index into a dispatch table. This replaces the current chain of `if (magic == 0x...)` checks with a table lookup.

```cpp
PropertyResolver* get_resolver(void* obj) {
    uint32_t magic0 = *(uint32_t*)obj;
    if (magic0 == FLAT_MAGIC) return &flat_resolver;
    if (magic0 == ARRAY_MAGIC) return &array_resolver;
    if (magic0 == STRING_MAGIC) return &string_resolver;
    uint32_t magic16 = *(uint32_t*)((char*)obj + 16);
    if (magic16 == MAP_MAGIC) return &map_resolver;
    if (magic16 == CLOSURE_MAGIC) return &closure_resolver;
    if (magic16 == FUNCTION_MAGIC) return &function_resolver;
    // Native C++ objects: use vtable
    return &native_resolver;
}
```

**Option B: Store resolver pointer in object header**
Add a `PropertyResolver*` field to every object. 8 bytes overhead per object but O(1) dispatch. This is essentially what V8 does with its Map pointer and JSC does with its Structure pointer.

### How to Use HIR/LLVM for Consistent Access

The compiler already has the information to emit correct property access. The key changes:

**1. HIR `GetPropStatic` should emit a guard + fast path**
```
; For typed objects with known layout:
if (obj->magic == expected_magic) {
    ; Fast path: direct field access
    result = GEP + load
} else {
    ; Slow path: call unified ts_property_get
    result = call ts_property_get(obj, "key")
}
```

**2. HIR `SetPropStatic` should check for setters on the slow path**
Currently the compiler's static set path never checks for setters. It should fall back to the runtime unified setter when the type isn't fully known.

**3. HIR should track whether an object "escapes to dynamic land"**
If a typed object is passed to a function that treats it as `any`, the compiler should mark it as potentially needing the dynamic path. This is already partially done with escape analysis.

## Implementation Sequence

1. **Define `PropertyResolver` struct** — small interface, 5 function pointers
2. **Implement resolvers for each type** — extract logic from the current giant switch
3. **Implement unified chain walker** — replace the chain-walking logic in each branch
4. **Wire up `get_resolver()`** — magic-number dispatch (Option A)
5. **Migrate `ts_object_get_property` to use unified walker** — one type at a time
6. **Migrate `ts_object_set_prop_v` similarly**
7. **Re-enable Object.create 2-arg** — should work because chain walker handles all types uniformly
8. **Add compiler guard emission** — HIR emits guard + fast path / slow path

Steps 1-6 are runtime-only changes. Step 7 is the Express test. Step 8 is the compiler optimization.

## Risk Assessment

- **Performance**: The resolver dispatch adds one indirection per prototype level. For typed code on the fast path, zero change. For dynamic code, it replaces a chain of `if (magic == ...)` checks with a function pointer call — roughly equivalent or slightly faster.
- **Memory**: Option A (magic dispatch) adds zero per-object overhead. Option B (stored pointer) adds 8 bytes per object.
- **Correctness**: The whole point is to make the slow path correct by construction. One chain walker means one place to handle getters, setters, attributes, and prototypes.
- **Migration**: Can be done incrementally — migrate one type at a time while keeping the old code as fallback.

## RED TEAM TASK

**Evaluate this architecture proposal independently.** Specifically:

1. Is the `PropertyResolver` interface sufficient for JS property access semantics? What's missing? (Proxy traps? Symbol properties? Computed property access?)

2. Would the resolver dispatch be fast enough for Express's hot paths? Express accesses `this.app`, `this.settings`, `this.req`, `this.res` on every request. Each access goes through the dynamic path (native objects with prototype chains).

3. Is there a simpler alternative? Could we solve the Express problem without a full architectural change — e.g., by making native C++ objects use TsMap directly for all their properties (while keeping native methods as inline fast paths)?

4. How does this interact with the compiler's existing optimization passes? Would the guard + fast path pattern interfere with escape analysis, SROA, or inline caching?

5. The current Express hang (Object.create 2-arg, call #45) — would this architecture fix it? Or is the hang caused by something orthogonal (e.g., the event emitter not dispatching correctly when prototype chains are modified)?

6. What's the migration risk? Can this be done incrementally without breaking the 22 currently passing Express tests?
