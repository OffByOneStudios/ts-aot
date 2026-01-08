# CONF-009: Object.defineProperty, defineProperties, getOwnPropertyDescriptor

**Status:** In Progress
**Priority:** Medium
**Created:** 2026-01-08

## Overview

Implement ES5 property descriptor methods for Object:
- `Object.defineProperty(obj, prop, descriptor)` - define/modify a property
- `Object.defineProperties(obj, descriptors)` - define multiple properties
- `Object.getOwnPropertyDescriptor(obj, prop)` - get property descriptor

## Current State

All three methods are not implemented (❌ in conformance matrix).

## Implementation Plan

### Step 1: Property Descriptor Support

Property descriptors in JavaScript have these attributes:
- `value` - the value of the property
- `writable` - if true, value can be changed
- `enumerable` - if true, shows in for-in and Object.keys
- `configurable` - if true, can be deleted/modified
- `get` - getter function (accessor descriptor)
- `set` - setter function (accessor descriptor)

For our simplified implementation, we'll support:
- `value` - ✅ store the value
- `writable` - ⚠️ track but don't enforce (complex)
- `enumerable` - ⚠️ track but don't enforce (complex)
- `configurable` - ⚠️ track but don't enforce (complex)
- `get`/`set` - ❌ not supported initially

### Step 2: Runtime Implementation

```cpp
// In TsObject.cpp
TsValue* ts_object_defineProperty(TsValue* obj, TsValue* prop, TsValue* descriptor);
TsValue* ts_object_defineProperties(TsValue* obj, TsValue* descriptors);
TsValue* ts_object_getOwnPropertyDescriptor(TsValue* obj, TsValue* prop);
```

### Step 3: Analyzer Types

Add to Object global type.

### Step 4: Codegen

Add handlers in IRGenerator_Expressions_Calls_Builtin.cpp

## Test Cases

```typescript
// Test defineProperty
const obj = {};
Object.defineProperty(obj, 'x', { value: 42 });
console.log(obj.x === 42 ? "PASS" : "FAIL");

// Test getOwnPropertyDescriptor
const desc = Object.getOwnPropertyDescriptor(obj, 'x');
console.log(desc.value === 42 ? "PASS" : "FAIL");

// Test defineProperties
Object.defineProperties(obj, {
    a: { value: 1 },
    b: { value: 2 }
});
console.log(obj.a === 1 && obj.b === 2 ? "PASS" : "FAIL");
```

## Success Criteria

- [ ] Object.defineProperty basic implementation
- [ ] Object.defineProperties basic implementation
- [ ] Object.getOwnPropertyDescriptor basic implementation
- [ ] Test files pass
- [ ] Conformance matrix updated

## Notes

- Full property descriptor semantics (writable, configurable, enumerable enforcement) is complex
- Initial implementation focuses on value setting/getting
- Getters/setters not supported in this ticket
