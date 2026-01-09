# Conformance Feature Implementation Workflow

This document describes the standard workflow for implementing features from the conformance matrices.

## Overview

The conformance matrices track all features needed for TypeScript, ECMAScript, and Node.js compatibility:
- [TypeScript Features](../../docs/conformance/typescript-features.md) - 174 features
- [ECMAScript Features](../../docs/conformance/ecmascript-features.md) - 223 features
- [Node.js Features](../../docs/conformance/nodejs-features.md) - 610 features

## Workflow Steps

### Step 1: Choose a Feature

1. Review the conformance matrices and identify a feature marked ❌ (Not implemented) or ⚠️ (Partial)
2. Consider priority:
   - **Critical**: Required for most applications
   - **High**: Common use cases
   - **Medium**: Framework support
3. Check dependencies - some features require others to be implemented first
4. Prefer features that unblock multiple downstream capabilities

**Example selection criteria:**
```
Good choices:
- Features blocking test failures
- Features with many dependents
- Features commonly used in npm packages

Avoid:
- Features with complex dependencies not yet implemented
- Deprecated features (marked N/A)
- Features requiring major architectural changes (unless planned)
```

### Step 2: Create a Ticket

Create a ticket file in `docs/tickets/` with the following format:

**File:** `docs/tickets/CONF-XXX-feature-name.md`

```markdown
# CONF-XXX: Feature Name

**Status:** In Progress
**Category:** TypeScript | ECMAScript | Node.js
**Feature:** `feature.name()` or Feature Description
**Conformance Doc:** [Link to relevant section]

## Description

Brief description of what needs to be implemented.

## Baseline Test Results

Run the test suite BEFORE implementing and capture results:

```
$ python tests/node/run_tests.py
Results: XX/XX passed (XX%)

$ python tests/golden_ir/runner.py
Results: XX passed, X failed, X XFAIL
```

## Implementation Plan

1. [ ] Analyzer changes (if any)
2. [ ] Codegen changes (if any)
3. [ ] Runtime changes (if any)
4. [ ] Test file(s) to create/modify

## Files to Modify

- `src/compiler/analysis/Analyzer_*.cpp`
- `src/compiler/codegen/IRGenerator_*.cpp`
- `src/runtime/src/*.cpp`
- `src/runtime/include/*.h`

## Test Plan

- [ ] Unit test: `tests/node/category/test_name.ts`
- [ ] Golden IR test: `tests/golden_ir/typescript/feature_test.ts` (if applicable)
- [ ] JavaScript equivalent: `tests/node/category/test_name.js` (if applicable)

## Acceptance Criteria

- [ ] Feature works as specified
- [ ] All new tests pass
- [ ] No regression in existing tests
- [ ] Conformance matrix updated
```

### Step 3: Implement the Feature

Follow the standard development workflow:

1. **Read relevant documentation:**
   - `.github/instructions/adding-nodejs-api.instructions.md` (for Node.js APIs)
   - `.github/instructions/runtime-extensions.instructions.md` (for runtime changes)
   - `.claude/rules/runtime-safety.md` (for runtime code)
   - `.claude/rules/llvm-ir-patterns.md` (for codegen)

2. **Make changes in the correct order:**
   ```
   Analyzer → Codegen → Runtime
   ```

3. **Build after each change:**
   ```powershell
   cmake --build build --config Release
   ```

4. **Test incrementally:**
   ```powershell
   build/src/compiler/Release/ts-aot.exe examples/test.ts -o examples/test.exe
   ./examples/test.exe
   ```

5. **Use debug flags when needed:**
   ```powershell
   # Dump inferred types
   build/src/compiler/Release/ts-aot.exe examples/test.ts --dump-types

   # Dump generated IR
   build/src/compiler/Release/ts-aot.exe examples/test.ts --dump-ir
   ```

6. **For crashes, use auto-debug:**
   ```
   /auto-debug
   ```

### Step 4: Add Tests and Verify

1. **Create test file(s):**
   - Node.js API tests go in `tests/node/<category>/<test_name>.ts`
   - Golden IR tests go in `tests/golden_ir/typescript/<feature>.ts`
   - Create JavaScript equivalents for Node.js tests

2. **Run the full test suite:**
   ```powershell
   # Node.js API tests
   python tests/node/run_tests.py

   # Golden IR tests
   python tests/golden_ir/runner.py
   ```

3. **Verify no regressions:**
   - Compare results to baseline captured in ticket
   - All previously passing tests must still pass
   - XFAIL tests should not become failures

4. **Update the ticket with results:**
   ```markdown
   ## Final Test Results

   ```
   $ python tests/node/run_tests.py
   Results: XX/XX passed (XX%)  # Should be >= baseline

   $ python tests/golden_ir/runner.py
   Results: XX passed, X failed, X XFAIL  # No new failures
   ```
   ```

### Step 5: Update Conformance Matrix (MANDATORY)

**⚠️ CRITICAL: This step is MANDATORY. The conformance matrices are the single source of truth for project progress. Agents may not remember past work, so these matrices MUST be kept in sync.**

1. **Open the relevant conformance matrix:**
   - TypeScript: `docs/conformance/typescript-features.md`
   - ECMAScript: `docs/conformance/ecmascript-features.md`
   - Node.js: `docs/conformance/nodejs-features.md`

2. **Update the feature status:**
   - Change ❌ to ✅ (fully implemented) or ⚠️ (partial)
   - Add notes in the Notes column explaining any limitations
   - Example: `| \`Array.find()\` | ✅ | |` or `| \`Array.find()\` | ⚠️ | No thisArg support |`

3. **Update the coverage summary:**
   - Find the summary section at the bottom of the file
   - Recalculate: `Implemented / Total = XX%`
   - Update both the category total and overall total

4. **Verify the update:**
   ```powershell
   # Check your changes
   git diff docs/conformance/
   ```

**Example matrix update:**
```markdown
# Before
| `Array.find()` | ❌ | |

# After
| `Array.find()` | ✅ | |

# Summary section - before
| Arrays | 15 | 20 | 75% |

# Summary section - after
| Arrays | 16 | 20 | 80% |
```

### Step 6: Archive Ticket and Commit

1. **Move ticket to archive:**
   ```powershell
   mv docs/tickets/CONF-XXX-feature-name.md docs/tickets/archive/
   ```

2. **Update ticket status:**
   ```markdown
   **Status:** Complete
   **Completed:** YYYY-MM-DD
   ```

3. **Commit all changes (including conformance matrix!):**
   ```powershell
   git add .
   git commit -m "$(cat <<'EOF'
   Implement <feature name>

   - Add <brief description of changes>
   - Tests: X new tests, all passing
   - Conformance: <Category> coverage now XX%

   Ticket: CONF-XXX

   ```yaml
   kind: Conformance
   env: ecma|node|typescript
   name: <Feature Name>
   test: <path to test file>
   ```

   Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
   EOF
   )"
   ```

**⚠️ The commit MUST include:**
- Changes to `docs/conformance/*.md`
- The YAML conformance block with `kind`, `env`, `name`, and `test` fields

**YAML Field Values:**
- `kind`: Always `Conformance`
- `env`: One of `ecma`, `node`, or `typescript`
- `name`: The feature name as it appears in the conformance matrix
- `test`: Relative path to the test file (e.g., `tests/node/array/array_change_by_copy.ts`)

## Directory Structure

```
docs/
├── conformance/
│   ├── typescript-features.md    # TypeScript conformance matrix
│   ├── ecmascript-features.md    # ECMAScript conformance matrix
│   └── nodejs-features.md        # Node.js conformance matrix
├── tickets/
│   ├── CONF-001-optional-chaining.md    # Active tickets
│   ├── CONF-002-nullish-coalescing.md
│   └── archive/                          # Completed tickets
│       └── CONF-000-example.md
```

## Ticket Numbering

- `CONF-XXX` - Conformance feature tickets
- Start at `CONF-001` and increment
- Use descriptive kebab-case names after the number

## Quick Reference

| Step | Action | Command/Location |
|------|--------|------------------|
| 1 | Choose feature | `docs/conformance/*.md` (find ❌ or ⚠️) |
| 2 | Create ticket | `docs/tickets/CONF-XXX-name.md` |
| 3 | Implement | `src/compiler/`, `src/runtime/` |
| 4 | Test | `python tests/node/run_tests.py` |
| 5 | **Update matrix** | `docs/conformance/*.md` (change ❌ to ✅) |
| 6 | Archive & commit | `docs/tickets/archive/`, `git commit` |

**Remember:** Step 5 is MANDATORY. The conformance matrix is our only persistent record of progress.

## Example Ticket

See [docs/tickets/archive/CONF-000-example.md](../../docs/tickets/archive/CONF-000-example.md) for a complete example of a finished ticket.
