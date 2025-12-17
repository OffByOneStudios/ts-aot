# Epic 15: Object Literals

**Status:** Planned
**Parent:** [Phase 4 Meta Epic](./meta_epic.md)

## Overview
Support "Plain Old JavaScript Objects" (POJOs) for grouping data, distinct from the `Map` class.

## Milestones

### Milestone 15.1: Runtime Object
- **Runtime:** Implement `TsObject` (likely wrapping a `TsMap` or similar structure initially).
- **API:** `GetProperty`, `SetProperty`.

### Milestone 15.2: Compiler Support
- **AST:** Support `ObjectLiteralExpression` (`{ key: val }`).
- **Codegen:** Generate calls to create objects and populate properties.
- **Codegen:** Update `PropertyAccessExpression` to handle `TsObject`.

## Action Items
- [x] Implement `TsObject` in Runtime.
- [x] Add AST/Codegen support for Object Literals.
- [x] Test with `tests/integration/objects.ts`.
