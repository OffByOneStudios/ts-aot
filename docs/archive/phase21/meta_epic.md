# Phase 21: Advanced Runtime Engineering

**Status:** Planned
**Goal:** Optimize the runtime for deployment and performance.

## Core Objectives
1.  **Static Linking:** Produce single-file binaries.
2.  **Custom GC:** Implement a high-performance Garbage Collector tailored for TypeScript.

## Milestones

### Epic 110: Static Linking & Minification
- [ ] **ICU Removal/Linking:** Statically link ICU or replace with a lightweight alternative (e.g., `uni-algo`) if full ICU isn't needed.
- [ ] **OpenSSL Static:** Statically link OpenSSL.
- [ ] **LTO:** Enable full Link Time Optimization.

### Epic 111: Custom Garbage Collector
- [ ] **Design:** Generational, Mark-Sweep-Compact GC.
- [ ] **Write Barriers:** Implement write barriers in the compiler (IR generation).
- [ ] **Stack Scanning:** Implement precise stack scanning (using LLVM StackMaps).
