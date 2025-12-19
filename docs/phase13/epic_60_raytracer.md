# Epic 60: The Ray Tracer (Numeric Intensity)

**Status:** Planned
**Goal:** Implement a computationally intensive Ray Tracer to demonstrate type-driven optimization.

## Concept
Ray tracing is a classic benchmark for compiler performance. It involves millions of small vector operations, virtual method calls (intersecting different shapes), and recursion.

## Implementation Details

### The TypeScript Code
We will write idiomatic, strictly typed TypeScript:
```typescript
class Vector {
    constructor(public x: number, public y: number, public z: number) {}
    add(v: Vector): Vector { ... }
    dot(v: Vector): number { ... }
}

interface SceneObject {
    intersect(ray: Ray): Intersection | null;
}

class Sphere implements SceneObject { ... }
```

### The AOT Advantage
1.  **Struct Generation:** The compiler should identify `Vector` as a fixed-shape class and generate a C++ struct/class with `double x, y, z`.
2.  **Inlining:** Small methods like `add` and `dot` should be candidates for inlining by the C++ compiler (via generated code structure).
3.  **Unboxed Math:** Operations on `number` typed fields should compile directly to CPU floating-point instructions, avoiding V8's tagging/untagging checks.

## Tasks
- [ ] Create `examples/raytracer/`.
- [ ] Implement `Vector`, `Ray`, `Color` classes.
- [ ] Implement `Scene`, `Camera`, and `Material` types.
- [ ] Implement the main render loop (generating a PPM file).
- [ ] **Optimization Pass:** Ensure the `IRGenerator` produces optimal code for:
    - [ ] Property access on typed classes.
    - [ ] Arithmetic expressions.
    - [ ] Object allocation (escape analysis would be nice, but Boehm GC is fast enough for now).

## Benchmarking
- Compare render time for a 1024x1024 image against Node.js.
