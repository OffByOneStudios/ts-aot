# CONF-076: Implement JavaScript Prototype Chain

**Status:** Complete
**Completed:** 2026-01-13
**Category:** ECMAScript
**Feature:** Prototype chain inheritance
**Conformance Doc:** ES5 Object.create, Object.getPrototypeOf, Object.setPrototypeOf

## Description

Implement proper JavaScript prototype chain semantics. Currently:
- `Object.create(proto)` copies properties instead of setting prototype
- `Object.getPrototypeOf()` returns undefined
- `Object.setPrototypeOf()` does nothing
- Property lookup does not walk the prototype chain

## Current State

### Runtime (TsMap)
```cpp
class TsMap : public TsObject {
    void* impl;        // std::unordered_map<TsValue, TsValue>
    bool frozen;
    bool sealed;
    bool extensible;
    // NO prototype pointer!
};
```

### Property Lookup (ts_object_get_property)
1. Check if boxed/unboxed
2. Check magic numbers for special types
3. Direct hash map lookup in TsMap
4. Return undefined if not found (NO prototype chain walk)

### Class Inheritance
- Uses C++ vtable-based method dispatch
- `extends` works via constructor chaining
- Separate from prototype chain (nominal vs structural)

## Implementation Plan

### Phase 1: Runtime Foundation

#### Step 1.1: Add Prototype Pointer to TsMap

**File:** `src/runtime/include/TsMap.h`
```cpp
class TsMap : public TsObject {
public:
    // ... existing members ...

    // NEW: Prototype chain support
    TsMap* prototype = nullptr;

    // Getters/setters
    TsMap* GetPrototype() const { return prototype; }
    void SetPrototype(TsMap* proto) { prototype = proto; }

    // Check for cycles before setting
    bool WouldCreateCycle(TsMap* proto) const;
};
```

#### Step 1.2: Implement WouldCreateCycle

**File:** `src/runtime/src/TsMap.cpp`
```cpp
bool TsMap::WouldCreateCycle(TsMap* proto) const {
    if (!proto) return false;
    TsMap* current = proto;
    while (current) {
        if (current == this) return true;
        current = current->prototype;
    }
    return false;
}
```

### Phase 2: Property Lookup with Prototype Chain

#### Step 2.1: Create Helper Function

**File:** `src/runtime/src/TsObject.cpp`

```cpp
// New helper: lookup property walking prototype chain
TsValue* ts_object_get_property_chain(TsMap* obj, const char* key) {
    TsMap* current = obj;
    while (current) {
        // Check own properties
        TsValue result = current->Get(keyValue);
        if (result.type != ValueType::UNDEFINED) {
            return ts_value_box(result);
        }
        // Walk to prototype
        current = current->GetPrototype();
    }
    return ts_value_make_undefined();
}
```

#### Step 2.2: Modify ts_object_get_property

**File:** `src/runtime/src/TsObject.cpp`

After the current TsMap lookup:
```cpp
// Current code:
TsValue result = map->Get(keyValue);
if (result.type != ValueType::UNDEFINED) {
    return ts_value_box(result);
}
// Return undefined - NO prototype chain

// NEW code:
TsValue result = map->Get(keyValue);
if (result.type != ValueType::UNDEFINED) {
    return ts_value_box(result);
}
// Walk prototype chain
TsMap* proto = map->GetPrototype();
while (proto) {
    result = proto->Get(keyValue);
    if (result.type != ValueType::UNDEFINED) {
        return ts_value_box(result);
    }
    proto = proto->GetPrototype();
}
return ts_value_make_undefined();
```

### Phase 3: Object.create/getPrototypeOf/setPrototypeOf

#### Step 3.1: Fix Object.create

**File:** `src/runtime/src/TsObject.cpp`

```cpp
TsValue* ts_object_create(TsValue* proto) {
    TsMap* newObj = TsMap::Create();

    if (!proto || ts_value_is_null(proto)) {
        // Object.create(null) - no prototype
        newObj->SetPrototype(nullptr);
        return ts_value_make_object(newObj);
    }

    if (ts_value_is_undefined(proto)) {
        // Object.create(undefined) - use Object.prototype
        // For now, treat as null prototype
        newObj->SetPrototype(nullptr);
        return ts_value_make_object(newObj);
    }

    // Unbox proto
    void* protoRaw = ts_value_get_object(proto);
    if (!protoRaw) {
        newObj->SetPrototype(nullptr);
        return ts_value_make_object(newObj);
    }

    // Get proto as TsMap
    TsMap* protoMap = dynamic_cast<TsMap*>((TsObject*)protoRaw);
    if (!protoMap) {
        // Not a TsMap - check magic
        uint32_t magic = *(uint32_t*)((char*)protoRaw + 16);
        if (magic == TsMap::MAGIC) {
            protoMap = (TsMap*)protoRaw;
        }
    }

    // Set prototype - DO NOT copy properties
    newObj->SetPrototype(protoMap);
    return ts_value_make_object(newObj);
}
```

#### Step 3.2: Implement Object.getPrototypeOf

**File:** `src/runtime/src/TsObject.cpp`

```cpp
TsValue* ts_object_getPrototypeOf(TsValue* obj) {
    if (!obj) return ts_value_make_null();

    void* rawPtr = ts_value_get_object(obj);
    if (!rawPtr) rawPtr = (void*)obj;

    // Get as TsMap
    TsMap* map = nullptr;
    uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
    if (magic == TsMap::MAGIC) {
        map = (TsMap*)rawPtr;
    } else {
        map = dynamic_cast<TsMap*>((TsObject*)rawPtr);
    }

    if (!map) return ts_value_make_null();

    TsMap* proto = map->GetPrototype();
    if (!proto) return ts_value_make_null();

    return ts_value_make_object(proto);
}
```

#### Step 3.3: Implement Object.setPrototypeOf

**File:** `src/runtime/src/TsObject.cpp`

```cpp
TsValue* ts_object_setPrototypeOf(TsValue* obj, TsValue* proto) {
    if (!obj) return ts_value_make_undefined();

    void* rawPtr = ts_value_get_object(obj);
    if (!rawPtr) rawPtr = (void*)obj;

    // Get target as TsMap
    TsMap* map = nullptr;
    uint32_t magic = *(uint32_t*)((char*)rawPtr + 16);
    if (magic == TsMap::MAGIC) {
        map = (TsMap*)rawPtr;
    }

    if (!map) return obj;  // Can't set prototype on non-objects

    // Check extensibility
    if (!map->IsExtensible()) {
        // TypeError: Object is not extensible
        return obj;
    }

    // Handle null prototype
    if (!proto || ts_value_is_null(proto)) {
        map->SetPrototype(nullptr);
        return obj;
    }

    // Get proto as TsMap
    void* protoRaw = ts_value_get_object(proto);
    TsMap* protoMap = nullptr;
    if (protoRaw) {
        uint32_t protoMagic = *(uint32_t*)((char*)protoRaw + 16);
        if (protoMagic == TsMap::MAGIC) {
            protoMap = (TsMap*)protoRaw;
        }
    }

    if (!protoMap) return obj;

    // Check for cycles
    if (map->WouldCreateCycle(protoMap)) {
        // TypeError: Cyclic prototype chain
        return obj;
    }

    map->SetPrototype(protoMap);
    return obj;
}
```

### Phase 4: Update Related Operations

#### Step 4.1: Fix `in` Operator

**File:** `src/runtime/src/TsObject.cpp` - `ts_object_has_property`

```cpp
bool ts_object_has_property(void* obj, const char* key) {
    TsMap* map = getMapFromObject(obj);
    if (!map) return false;

    // Check own properties
    TsValue keyValue;
    keyValue.type = ValueType::STRING_PTR;
    keyValue.ptr_val = TsString::Create(key);

    // Walk prototype chain
    TsMap* current = map;
    while (current) {
        if (current->Has(keyValue)) {
            return true;
        }
        current = current->GetPrototype();
    }
    return false;
}
```

#### Step 4.2: Add Object.hasOwnProperty

**File:** `src/runtime/src/TsObject.cpp`

```cpp
bool ts_object_hasOwnProperty(void* obj, const char* key) {
    TsMap* map = getMapFromObject(obj);
    if (!map) return false;

    TsValue keyValue;
    keyValue.type = ValueType::STRING_PTR;
    keyValue.ptr_val = TsString::Create(key);

    // Only check own properties, NOT prototype chain
    return map->Has(keyValue);
}
```

#### Step 4.3: Fix Object.keys (already correct, but verify)

```cpp
// Object.keys should only return own enumerable properties
// NOT inherited properties - verify this is the current behavior
```

### Phase 5: Analyzer Updates (Optional Enhancement)

This phase is optional but would improve type checking for prototype-based code.

#### Step 5.1: Track Prototype in ObjectType

**File:** `src/compiler/analysis/Type.h`

```cpp
struct ObjectType : public Type {
    std::map<std::string, std::shared_ptr<Type>> fields;
    std::map<std::string, std::shared_ptr<FunctionType>> getters;
    std::map<std::string, std::shared_ptr<FunctionType>> setters;

    // NEW: Prototype tracking (optional)
    std::shared_ptr<ObjectType> prototype;

    // Combined field lookup (own + inherited)
    std::shared_ptr<Type> getField(const std::string& name) const {
        auto it = fields.find(name);
        if (it != fields.end()) return it->second;
        if (prototype) return prototype->getField(name);
        return nullptr;
    }
};
```

### Phase 6: Testing

#### Step 6.1: Create Test File

**File:** `tests/node/object/prototype_chain.ts`

```typescript
function user_main(): number {
    console.log("=== Prototype Chain Test ===");

    // Test 1: Object.create with prototype
    console.log("Test 1: Object.create");
    const proto = { x: 10, greet() { return "hello"; } };
    const obj = Object.create(proto);
    console.log(obj.x);  // 10 (inherited)

    // Test 2: Own property shadows prototype
    console.log("Test 2: Shadowing");
    obj.x = 20;
    console.log(obj.x);  // 20 (own)
    console.log(proto.x);  // 10 (unchanged)

    // Test 3: Prototype changes affect descendants
    console.log("Test 3: Dynamic prototype");
    const child = Object.create(proto);
    proto.y = 30;
    console.log(child.y);  // 30 (inherited after creation)

    // Test 4: Object.getPrototypeOf
    console.log("Test 4: getPrototypeOf");
    const p = Object.getPrototypeOf(obj);
    console.log(p === proto);  // true

    // Test 5: Object.create(null)
    console.log("Test 5: null prototype");
    const nullProto = Object.create(null);
    console.log(Object.getPrototypeOf(nullProto));  // null

    // Test 6: in operator with prototype chain
    console.log("Test 6: in operator");
    console.log("x" in obj);  // true (own)
    console.log("greet" in obj);  // true (inherited)
    console.log("z" in obj);  // false

    // Test 7: hasOwnProperty
    console.log("Test 7: hasOwnProperty");
    console.log(obj.hasOwnProperty("x"));  // true (own)
    console.log(obj.hasOwnProperty("greet"));  // false (inherited)

    // Test 8: Multi-level prototype chain
    console.log("Test 8: Multi-level chain");
    const grandparent = { a: 1 };
    const parent = Object.create(grandparent);
    parent.b = 2;
    const child2 = Object.create(parent);
    child2.c = 3;
    console.log(child2.a);  // 1 (from grandparent)
    console.log(child2.b);  // 2 (from parent)
    console.log(child2.c);  // 3 (own)

    console.log("=== Tests Complete ===");
    return 0;
}
```

## Files to Modify

### Runtime
- `src/runtime/include/TsMap.h` - Add prototype pointer
- `src/runtime/src/TsMap.cpp` - Add cycle detection
- `src/runtime/src/TsObject.cpp` - Update property lookup, fix Object.* methods

### Analyzer (Optional)
- `src/compiler/analysis/Type.h` - Track prototype in ObjectType

### Tests
- `tests/node/object/prototype_chain.ts` - New test file

## Acceptance Criteria

- [x] `Object.create(proto)` sets prototype, does NOT copy properties
- [x] `Object.getPrototypeOf(obj)` returns the prototype object
- [x] `Object.setPrototypeOf(obj, proto)` changes the prototype
- [x] Property lookup walks prototype chain
- [x] `in` operator checks prototype chain
- [x] `hasOwnProperty` only checks own properties
- [x] Prototype cycles are detected and prevented
- [x] `Object.create(null)` creates object with null prototype
- [x] Multi-level prototype chains work
- [x] Existing tests still pass
- [x] Conformance matrix updated

## Risk Assessment

### High Risk
- **Performance**: Every property access now walks prototype chain
  - Mitigation: Most objects don't have long chains
  - Future: Implement inline caching

### Medium Risk
- **Breaking Changes**: Existing code might rely on copy behavior
  - Mitigation: Run full test suite

### Low Risk
- **Memory**: Prototype pointer adds 8 bytes per TsMap
  - Impact: Negligible

## Estimated Complexity

- Runtime changes: Medium
- Analyzer changes: Low (optional)
- Testing: Low
- Total: Medium

## Dependencies

None - this is a foundational feature.

## Notes

- This implementation focuses on runtime semantics
- Class-based inheritance (extends) still uses vtables
- Prototype chain is for object literal inheritance
- Future optimization: hidden classes for common patterns
