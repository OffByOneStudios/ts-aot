# TypeScript AOT Compiler (ts-aot)

You are an expert C++ and TypeScript developer working on `ts-aot`, an Ahead-of-Time compiler for TypeScript.

## Project Overview

This is an LLVM-based ahead-of-time compiler that compiles TypeScript directly to native executables. The compiler generates optimized machine code while maintaining JavaScript semantics and Node.js API compatibility.

**Current Status:** Conformance-driven development

## Essential Documentation

**Read these at the start of every session:**
- @.github/context/active_state.md - Current phase, recent accomplishments, active tasks
- @.github/instructions/conformance-workflow.instructions.md - **Feature implementation workflow**

**Conformance Matrices (feature tracking):**
- @docs/conformance/typescript-features.md - TypeScript features (174 total, 41% implemented)
- @docs/conformance/ecmascript-features.md - ECMAScript features (223 total, 36% implemented)
- @docs/conformance/nodejs-features.md - Node.js APIs (610 total, 20% implemented)

**Reference documentation (consult as needed):**
- @.github/DEVELOPMENT.md - Detailed development guidelines
- @.github/context/architecture_decisions.md - Key architectural choices
- @.github/context/known_issues.md - Current limitations and technical debt
- @.github/instructions/quick-reference.md - Quick lookup for common patterns
- @.github/instructions/code-snippets.md - Copy-paste ready code templates
- @.github/instructions/runtime-extensions.instructions.md - How to extend the runtime
- @.github/instructions/adding-nodejs-api.instructions.md - How to add Node.js APIs

## Skills Available

This project includes Claude Code skills for automated tasks:

### Auto-Debug Skill (`/auto-debug`)
**Trigger terms:** crash, access violation, debug, analyze crash, CDB, debugger
**Location:** `.claude/skills/auto-debug/`

Automatically analyzes crashes using CDB debugger. Extracts stack traces, exception info, and crash locations.

**⚠️ MANDATORY:** Always use this skill for crash analysis. **NEVER** invoke `cdb` directly.

### Golden IR Tests Skill (`/golden-ir-tests`)
**Trigger terms:** golden tests, IR tests, regression tests, test runner
**Location:** `.claude/skills/golden-ir-tests/`

Run the golden IR test suite to validate compiler correctness and prevent regressions.

### CTag Search Skill (`/ctags-search`)
**Trigger terms:** find symbol, search definition, ctags
**Location:** `.claude/skills/ctags-search/`

Search for symbol definitions using ctags. Preferred over grep for finding function/class definitions.

## Project Structure

```
src/
├── compiler/           # Host compiler (runs on dev machine)
│   ├── analysis/      # Type inference and semantic analysis
│   ├── ast/           # AST loading and processing
│   └── codegen/       # LLVM IR generation
├── runtime/           # Target runtime (linked into generated code)
│   ├── include/       # Runtime headers
│   └── src/           # Runtime implementation
examples/              # Production-ready examples and benchmarks ONLY
├── benchmarks/        # Performance comparison suite
└── production/        # Real-world application templates
tmp/                   # Temporary test/debug files (use this for ad-hoc testing!)
tests/
├── node/             # Node.js API tests (.ts and .js)
└── golden_ir/        # Golden IR regression tests
    ├── typescript/   # Typed code tests
    └── javascript/   # Dynamic code tests
docs/
├── conformance/      # Feature conformance matrices
├── tickets/          # Active implementation tickets
│   └── archive/      # Completed tickets
└── archive/          # Archived phase documentation
```

## ⛔ CRITICAL: File Location Rules

**NEVER create test files or debug scripts in `examples/`**

| File Type | Correct Location |
|-----------|------------------|
| Temporary tests, debug scripts | `tmp/` |
| Bug reproductions | `tmp/` |
| Benchmarks | `examples/benchmarks/` |
| Production templates | `examples/production/` |
| Conformance tests | `tests/node/` or `tests/golden_ir/` |

The `examples/` directory is reserved for polished, production-ready code only.

## Core Development Workflow

### Conformance Feature Implementation

**Follow this cycle when implementing conformance features:**

1. **Choose:** Pick a feature from `docs/conformance/*.md` (marked ❌ or ⚠️)
2. **Ticket:** Create `docs/tickets/CONF-XXX-feature-name.md` with baseline test results
3. **Implement:** Make changes to Analyzer → Codegen → Runtime
4. **Test:** Run full test suite, verify no regressions
5. **Update Matrix:** Change ❌ to ✅ in `docs/conformance/*.md` **(MANDATORY)**
6. **Archive:** Move ticket to `docs/tickets/archive/`, commit

**⚠️ CRITICAL:** Always update the conformance matrix (Step 5). It is the single source of truth for project progress. Agents may not remember past work.

See @.github/instructions/conformance-workflow.instructions.md for detailed steps.

### General Development Cycle

1. **Context:** Read `.github/context/active_state.md` to understand current tasks
2. **Plan:** Use TodoWrite tool to break down tasks
3. **Search:** Use `/ctags-search` skill for symbol lookups
4. **Implement:** Write code following technical constraints
5. **Build:** `cmake --build build --config Release` (ALWAYS build ALL targets)
6. **Verify:**
   - Run compiler: `build/src/compiler/Release/ts-aot.exe tmp/test.ts -o tmp/test.exe`
   - Debug crashes: Use `/auto-debug` skill
   - Check types: Use `--dump-types` flag
   - Run tests: `python tests/node/run_tests.py` and `python tests/golden_ir/runner.py`
7. **Commit:** `git add . && git commit` with descriptive message referencing ticket

## Code Style and Standards

See `.claude/rules/` for detailed language-specific standards:
- @.claude/rules/runtime-safety.md - Critical runtime memory/casting rules
- @.claude/rules/llvm-ir-patterns.md - LLVM 18 IR generation patterns
- @.claude/rules/typescript-conventions.md - TypeScript code conventions

## Communication Style

- Be concise and direct - this is a CLI tool
- Use GitHub-flavored markdown
- **Never use emojis** unless explicitly requested
- Use code references in format: `file_path:line_number`
- Don't create unnecessary files (especially markdown documentation)
- Prioritize technical accuracy over validation

## Key Technical Notes

**Language:** C++20
**Build System:** CMake + vcpkg
**LLVM Version:** 18 (opaque pointers)
**Memory Management:** Boehm GC via `ts_alloc`
**Async I/O:** libuv
**Strings:** TsString (ICU-based)

## Critical Safety Rules

Before editing **ANY** file in `src/runtime/`:

| Task | ✅ CORRECT | ❌ WRONG |
|------|-----------|----------|
| Allocate object | `ts_alloc(sizeof(T))` + placement new | `new T()` or `malloc` |
| Create string | `TsString::Create("...")` | `std::string` |
| Cast base/derived | `obj->AsEventEmitter()` or `dynamic_cast<T*>` | `(T*)ptr` C-style cast |
| Unbox void* param | `ts_value_get_object((TsValue*)p)` | Assume it's raw pointer |
| Create error | `ts_error_create("msg")` | Double-box with `ts_value_make_object` |
| Use `any` value | Always unbox with `ts_value_get_object()` | Check `boxedValues.count()` |

See @.claude/rules/runtime-safety.md for complete details.

## Modular Rules

Rules in `.claude/rules/` are automatically applied based on file paths:
- Files matching `src/runtime/**` → runtime-safety.md
- Files matching `src/compiler/codegen/**` → llvm-ir-patterns.md
- Files matching `examples/**/*.ts` → typescript-conventions.md

## Notes on AI Assistance

- Use TodoWrite tool frequently to track progress
- Mark todos as completed immediately after finishing
- Ask questions via AskUserQuestion tool when clarification needed
- Use parallel tool calls when operations are independent
- Always investigate to find truth before confirming user beliefs
- Provide objective guidance even when it disagrees with user assumptions
