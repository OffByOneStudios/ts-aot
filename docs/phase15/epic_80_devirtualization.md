# Epic 80: Devirtualization

**Status:** Planned
**Goal:** Convert indirect (vtable) calls into direct function calls to enable inlining and reduce branch mispredictions.

## Background
Currently, all method calls in `ts-aot` go through a vtable lookup (indirect call). This prevents LLVM from inlining methods and adds overhead to every call. Since our monomorphizer creates specialized versions of functions for specific types, we often know the exact class of an object at a call site.

## Action Items

### Milestone 1: Infrastructure
- [ ] Add `concreteType` information to `llvm::Value*` tracking in `IRGenerator`.
- [ ] Implement a lookup mechanism to find the concrete function implementation for a given class and method name.

### Milestone 2: Static Devirtualization
- [ ] Identify call sites where the receiver is a result of `new ClassName()`.
- [ ] Replace the `vtable` load and indirect call with a direct call to `ClassName_methodName`.
- [ ] Verify that LLVM inlines these calls at `-O3`.

### Milestone 3: Monomorphization-based Devirtualization
- [ ] Use the `Specialization` context to identify when a parameter or local variable has a known concrete class type.
- [ ] Apply devirtualization to these call sites.

## Verification Plan
- **IR Inspection:** Ensure `call ptr %vtable_ptr` is replaced by `call void @Vector3_add`.
- **Benchmark:** Measure the impact on the Raytracer (which is heavy on `Vector3` method calls).
