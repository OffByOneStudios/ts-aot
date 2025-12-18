# Epic 50: Advanced Function Features

**Status:** Not Started
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Support for optional, default, and rest parameters.

## Milestones

### Milestone 50.1: Optional & Default Parameters
- **Analysis:** Allow calls with fewer arguments than parameters.
- **Codegen:** Emit default value assignments if arguments are missing.

### Milestone 50.2: Rest Parameters
- **Analysis:** Collect remaining arguments into an array.
- **Codegen:** Create a `TsArray` from the variadic arguments.

## Action Items
- [ ] Update `visitCallExpression` to handle missing optional/default arguments.
- [ ] Implement rest parameter array construction in `IRGenerator_Functions.cpp`.
