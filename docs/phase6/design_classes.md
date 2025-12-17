# Design: Classes & VTables (Phase 6)

To achieve high-performance dynamic dispatch similar to C++ or Rust, `ts-aot` will implement a VTable-based memory layout for Class instances.

## 1. Memory Layout

### Instance Layout
Every instance of a class will be a struct where the first element is a pointer to the VTable.

```cpp
// LLVM IR Structure for class Point { x: number; y: number; print(): void; }
struct PointInstance {
    PointVTable* vptr;  // Hidden first field
    double x;
    double y;
}
```

### VTable Layout
The VTable is a global constant struct containing pointers to the implementation of the methods.

```cpp
struct PointVTable {
    // Metadata (Optional: for instanceof checks)
    void* typeInfo; 
    
    // Virtual Methods
    void (*print)(PointInstance* this);
    double (*distance)(PointInstance* this, PointInstance* other);
}
```

## 2. Inheritance & VTable Construction

When a class `Child` extends `Parent`:
1.  **Prefix Matching:** The `Child` VTable starts with the exact same layout as `Parent` VTable.
2.  **Overriding:** If `Child` overrides a method, the function pointer in the `Child` VTable is replaced with the `Child` implementation.
3.  **Extension:** New methods in `Child` are appended to the end of the VTable.

```cpp
class Parent { foo() {} }
class Child extends Parent { bar() {} }

// Parent VTable: [ &Parent::foo ]
// Child VTable:  [ &Parent::foo, &Child::bar ]
```

## 3. Dispatch Mechanism

For a method call `obj.method()`:

1.  **Load VPtr:** Load the `vptr` from index 0 of `obj`.
2.  **Calculate Offset:** We know at compile-time that `method` is at index `K` in the VTable.
3.  **Load Function:** Load the function pointer from `vptr[K]`.
4.  **Call:** `call func(obj, args...)`.

## 4. Structural Typing vs Classes

TypeScript allows structural typing (Duck Typing).
- **Class-to-Class:** If we know the type is a Class, we use the VTable (fast).
- **Interface-to-Class:** If we have an interface `interface I { foo(): void }`, and we pass a class instance, we have two options:
    1.  **Fat Pointers (Rust style):** Pass `{ instance*, vtable* }`.
    2.  **Interface Tables (Java style):** Look up the method in a secondary table.
    
    *Decision for Phase 6:* We will prioritize **Class-based VTables** first. Interfaces will likely be handled via a canonical VTable layout if the interface is implemented by the class, or a slower lookup fallback if not.

## 5. Implementation Plan

1.  **Analyzer:**
    *   Track class hierarchy.
    *   Assign indices to methods (vtable slots).
    *   Calculate struct layout (field offsets).

2.  **IRGenerator:**
    *   Generate `StructType` for each class.
    *   Generate Global Constant for each class's VTable.
    *   In `visitNewExpression`: Store the VTable pointer in the new instance.
    *   In `visitCallExpression`: Emit the load-vptr-load-func-call sequence.
