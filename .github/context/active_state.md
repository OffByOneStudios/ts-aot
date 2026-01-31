# Active Project State

**Last Updated:** 2026-01-30
**Current Phase:** Performance Optimization + HIR Testing

## Current Focus
1. Optimizing runtime performance to achieve parity with Node.js
2. Building comprehensive HIR test coverage

## Active Tasks
1. **Performance Epic** - Planning complete, see `docs/epics/EPIC-PERF-001-performance-parity.md`
2. **HIR Golden Tests** - 58/183 tests (32%), all passing. See `docs/tickets/HIR-001-golden-hir-tests.md`
3. **Next: Unboxed Arrays** - Highest impact/effort ratio optimization

## Recent Accomplishments (2026-01-30)
*   **HIR Test Suite Complete:** All 58 golden HIR tests passing (100%)
*   **Bitwise Operations Fixed:** Added f64→i64 conversion in HIRToLLVM for bitwise ops
*   **Dead Code Elimination Fixed:** Added terminator checks to stop statement processing after return/throw
*   **Generator Support:** Full state machine transformation for function* and yield
*   **BigInt Support:** Runtime calls for arbitrary precision arithmetic
*   **Class Expressions:** Fixed naming alignment and vtable registration

## Lessons Learned (for future sessions)
1. **Check all code paths**: When fixing a pattern (like terminator handling), search for ALL places it occurs (visitBlockStatement, visitFunctionDeclaration, visitArrowFunction)
2. **Type conversions in lowering**: TypeScript numbers are f64, but some LLVM ops need i64 (bitwise). Always add conversion helpers.
3. **Run full test suite**: After any fix, run the complete test suite to catch regressions
4. **Update docs immediately**: Update ticket and epic docs before committing to maintain accurate project state

## Conformance Status
- TypeScript: 99% (117/118 runtime features)
- ECMAScript: 93% (214/230 features)
- Node.js: 99% (1031/1040 features)

## Performance Optimization Roadmap

| Phase | Optimization | Expected Speedup | Status |
|-------|--------------|------------------|--------|
| 1 | Unboxed Arrays | 2-3x | Planned |
| 2 | SMI (Tagged Integers) | 3-5x cumulative | Planned |
| 3 | Hidden Classes/Shapes | 5-10x cumulative | Planned |
| 4 | Method Inline Cache | 6-12x cumulative | Planned |
| 5 | Escape Analysis | Variable | Planned |
| 6 | String Interning | +10-20% | Planned |
| 7 | Custom Generational GC | Better latency | Future |

See [EPIC-PERF-001](../docs/epics/EPIC-PERF-001-performance-parity.md) for details.
