# ARCH: Unified Property Access Protocol v2 (Red-Team Revised)

## Summary of Red Team Feedback

The v1 proposal was directionally correct but too broad, too optimistic about migration, and under-specified the abstraction. Both red teams agreed:

1. **Fix Object.create 2-arg FIRST** — the native wrapper simply drops argv[1], this is a concrete bug
2. **The PropertyResolver interface needs descriptors, not raw values** — must distinguish absent/data/accessor, support symbol keys, receiver-aware get/set for Proxy/Reflect
3. **Don't touch compiler fast paths (SROA, escape analysis) in phase 1** — guard emission is more invasive than stated
4. **Start with native objects only** — they're the actual cross-boundary failure point

## Revised Phased Plan

### Phase 1: Fix Object.create 2-arg (the actual broken path)

The `ts_object_create_native` wrapper drops `argv[1]`. Fix it. But the previous 8 attempts caused 7 Express regressions (server starts, `res.json()` completes, but HTTP response never reaches client).

**The remaining mystery:** WHY does enabling descriptors cause responses to hang when `res.json()` demonstrably runs to completion? 

Investigate:
- Native response lookup order: virtual dispatch → side-map → prototype. Does adding `app` as an own property on the prototype change which branch fires?
- Exception behavior: trace whether `res.end()` / `res.writeHead()` throws silently when the prototype chain has extra properties
- The `res.send()` → `res.end()` → libuv write path: is there a condition where write never flushes?

### Phase 2: Descriptor-based own-property protocol for native objects

Define a protocol that native C++ objects implement for JS property access:

```cpp
struct OwnPropertyResult {
    enum Kind { NOT_FOUND, DATA, ACCESSOR };
    Kind kind;
    TsValue value;        // for DATA
    TsValue getter;       // for ACCESSOR
    TsValue setter;       // for ACCESSOR  
    uint8_t attributes;   // enumerable|writable|configurable
};

// Each native object type provides:
OwnPropertyResult (*getOwnProperty)(void* obj, TsValue key);
bool (*defineOwnProperty)(void* obj, TsValue key, OwnPropertyResult desc);
bool (*deleteOwnProperty)(void* obj, TsValue key);
void* (*ownKeys)(void* obj);  // returns TsArray*
void* (*getPrototype)(void* obj);
void (*setPrototype)(void* obj, void* proto);
```

This is richer than v1's raw value interface. Key improvements per red team:
- Returns descriptors, not values
- Takes TsValue keys (supports symbols)
- Separate define vs set semantics
- ownKeys for enumeration

### Phase 3: "Fat Native Object" — embed TsMap in native objects

Replace the global `g_native_object_props` side-map with an embedded `TsMap*` field on native C++ objects (TsServerResponse, TsIncomingMessage, TsSocket, etc.).

Benefits:
- TsMap already supports prototype chains, getters, setters, property attributes
- Eliminates the side-map hash table lookup indirection
- Native methods resolve via virtual dispatch first, then fall back to the embedded TsMap
- The embedded TsMap's prototype chain provides JS inheritance semantics

This is Gemini's "Fat Native Object" suggestion, which is a specific implementation of GPT's "unify native objects with TsMap" recommendation.

### Phase 4: Unify chain walker (only if needed)

If phases 1-3 fix Express, stop here. If not, then unify the prototype chain walker across ALL object types using the phase 2 protocol. But defer this — arrays, strings, functions, and flat objects don't need it for Express.

### Phase 5: Compiler guard emission (future)

Only after the runtime is correct: emit typed fast path with dynamic fallback guards. Must be a LATE lowering pass that runs AFTER escape analysis and SROA, not a change to HIR semantics.

## What NOT to do

- Don't change GetPropStatic/SetPropStatic HIR semantics
- Don't migrate arrays, strings, or flat objects until native objects are fixed
- Don't assume the Express hang is in generic property access — investigate the actual response write path
- Don't use Option A (magic number dispatch table) — too branchy per Gemini
- Don't use `const char*` keys — need TsValue for symbol support per GPT

## Open Questions

1. The Express hang mystery: `res.json()` runs to completion (verified by tracing) but the HTTP response never reaches the client. Is this a libuv write path issue? A native response object state corruption? An exception being swallowed?

2. Would embedding TsMap in native objects change the object layout enough to break existing vtable dispatch or virtual method calls?

3. The `Reflect` implementation (TsReflect.cpp) still says "no prototype chain support." Does it need to be unified too, or can it be deferred?
