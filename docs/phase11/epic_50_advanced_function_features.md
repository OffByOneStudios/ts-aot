# Epic 50: Advanced Function Features

**Status:** Completed
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Support for optional, default, and rest parameters.

## Milestones

### Milestone 50.1: Optional & Default Parameters
- [x] **Analysis:** Allow calls with fewer arguments than parameters.
- [x] **Codegen:** Emit default value assignments if arguments are missing.

### Milestone 50.2: Rest Parameters
- [x] **Analysis:** Collect remaining arguments into an array.
- [x] **Codegen:** Create a `TsArray` from the variadic arguments.

## Action Items
- [x] Update `visitCallExpression` to handle missing optional/default arguments.
- [x] Implement rest parameter array construction in `IRGenerator_Functions.cpp`.
- [x] Support compound assignment operators (+=, -=, etc.) in `IRGenerator_Expressions.cpp`.
- [x] Update `TsArray` runtime and IR signatures to use `TsValue*` (ptr) for elements.
