# Epic 51: Advanced Types & Parity

**Status:** Not Started
**Parent:** [Phase 11 Meta Epic](./meta_epic.md)

## Overview
Finalize the type system with `unknown`, `never`, and better `null`/`undefined` handling.

## Milestones

### Milestone 51.1: unknown & never
- **unknown:** Implement as a top type that requires narrowing.
- **never:** Implement as a bottom type for unreachable code.

### Milestone 51.2: Strict Null Checks
- **Analysis:** (Optional) Implement basic checks for `null` and `undefined` access.

## Action Items
- [ ] Add `unknown` and `never` to the `Type` system.
- [ ] Implement narrowing logic for `unknown`.
