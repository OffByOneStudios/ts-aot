# Claude Code Configuration for ts-aot

This directory contains the Claude Code configuration for the TypeScript AOT compiler project.

## Directory Structure

```
.claude/
├── README.md              # This file
├── settings.json          # Project permissions and hooks
├── rules/                 # Modular rules auto-applied by file path
│   ├── runtime-safety.md  # Memory safety (src/runtime/**)
│   ├── llvm-ir-patterns.md # IR codegen (src/compiler/codegen/**)
│   └── typescript-conventions.md # Test files (examples/**)
└── skills/                # Custom skills for specialized tasks
    ├── auto-debug/        # CDB crash analysis
    │   └── SKILL.md
    ├── golden-ir-tests/   # Test suite runner
    │   └── SKILL.md
    └── ctags-search/      # Symbol definition search
        └── SKILL.md
```

## Main Files

### CLAUDE.md (in project root)
Main project instructions loaded automatically at session start. Contains:
- Project overview and current status
- Links to essential documentation
- Development workflow
- Core technical constraints
- Communication style guidelines

### settings.json
Project-level configuration for:
- **Permissions**: What tools and files Claude can access
- **Hooks**: Automatic reminders and checks
  - SessionStart: Load active state
  - PostToolUse: Build reminders after edits
  - PreToolUse: Block dangerous operations

### Rules
Domain-specific guidelines that auto-apply based on file paths:

**runtime-safety.md** (src/runtime/**)
- Memory allocation with ts_alloc
- String creation with TsString::Create
- Safe casting with AsXxx() helpers
- Boxing/unboxing conventions
- Virtual inheritance patterns

**llvm-ir-patterns.md** (src/compiler/codegen/**)
- LLVM 18 opaque pointer patterns
- GEP with source types
- Load/Store with value types
- Function call type matching
- Boxing/unboxing in codegen

**typescript-conventions.md** (examples/**)
- Test file structure (user_main)
- Available features and limitations
- Golden IR test format
- Performance considerations

### Skills
Specialized tools that activate based on trigger terms:

**auto-debug** - Crash analysis
- Triggers: "crash", "access violation", "debug", "CDB"
- Runs CDB debugger script to analyze crashes
- Extracts stack traces and exception info
- Never invoke CDB directly - always use the skill

**golden-ir-tests** - Test runner
- Triggers: "golden tests", "regression tests", "run tests"
- Runs Python test suite in tests/golden_ir/
- Reports pass/fail/XFAIL status
- Validates IR patterns and runtime output

**ctags-search** - Symbol search
- Triggers: "find symbol", "where is defined", "locate function"
- Searches for symbol definitions using ctags
- Preferred over grep for finding definitions
- Returns file path and line number

## How It Works

### Session Start
1. Claude Code loads `CLAUDE.md` automatically
2. Shows reminder to read active state
3. Makes all skills available
4. Applies permissions from settings.json

### While Editing
1. Rules auto-apply based on file path
2. Hooks show reminders after edits
3. Permissions enforce safety
4. Skills activate on trigger terms

### Example Flow

**Editing runtime code:**
```
1. Edit src/runtime/TsArray.cpp
2. runtime-safety.md rules apply automatically
3. After save, PostToolUse hook reminds: "Check safety patterns"
4. Build reminder shown
```

**Debugging a crash:**
```
1. User says: "examples/test.exe crashes"
2. Trigger term "crashes" activates auto-debug skill
3. Claude runs: .github/skills/auto-debug/debug_analyzer.ps1
4. Analyzes stack trace and suggests fix
```

**Finding a symbol:**
```
1. User asks: "Where is ts_array_get defined?"
2. Trigger term "where is defined" activates ctags-search skill
3. Claude runs: .github/skills/ctags-search/search_tags.ps1
4. Returns: src/runtime/src/TsArray.cpp:145
```

## Customization

### Personal Settings (Not Committed)

Create `.claude/CLAUDE.local.md` for personal preferences:
```markdown
# My Personal Claude Config

I prefer verbose explanations.
Always run tests after implementing features.
```

Create `.claude/settings.local.json` for personal permissions:
```json
{
  "model": "claude-opus-4-5",
  "permissions": {
    "allow": ["Bash(my-custom-tool:*)"]
  }
}
```

These files are gitignored and won't be shared with the team.

### Adding New Rules

Create `.claude/rules/my-rule.md`:
```yaml
---
paths: src/my-component/**
priority: high
---

# My Component Rules

Guidelines specific to my component...
```

Restart Claude Code to load the new rule.

### Adding New Skills

Create `.claude/skills/my-skill/SKILL.md`:
```yaml
---
name: my-skill
description: What this skill does. Include trigger terms like "my", "skill", "custom".
allowed-tools: Bash, Read, Write
---

# My Skill

Instructions for Claude to follow when this skill is used...
```

Restart Claude Code to load the new skill.

## Permissions

### Allowed
- Git operations (add, commit, status, diff, log)
- Build operations (cmake, make, python)
- Reading source files (src/, docs/, examples/, tests/)
- Editing source files (src/, examples/, tests/)
- Running skill scripts

### Blocked
- Direct CDB invocation (use auto-debug skill)
- Destructive operations (rm, del, format)
- Network operations (curl, wget)
- Reading sensitive files (.env, secrets/)
- Editing critical files (CMakeLists.txt, workflows)
- Writing to build directories

## Hooks

### SessionStart
Shows reminder to read `.github/context/active_state.md` for current phase and tasks.

### PostToolUse
- After editing compiler code → "Remember to build"
- After editing runtime code → "Check runtime safety patterns"

### PreToolUse
- Blocks direct CDB invocation → "Use auto-debug skill instead"

## Migration from Copilot

This project was migrated from GitHub Copilot. The original structure is preserved:

**Preserved in `.github/`:**
- `copilot-instructions.md` - Original instructions (for reference)
- `DEVELOPMENT.md` - Development guidelines
- `context/` - Active state and architecture decisions
- `instructions/` - Code snippets and how-tos
- `skills/` - Original PowerShell scripts (still functional)

**New in `.claude/` and root:**
- `CLAUDE.md` - Main instructions (replaces copilot-instructions.md)
- `.claude/` - Modular rules, skills, settings
- `MIGRATION.md` - Migration guide

See [MIGRATION.md](../MIGRATION.md) for full details.

## Troubleshooting

**Skills not activating?**
- Check skill description includes relevant trigger terms
- Explicitly ask: "Use the auto-debug skill"

**Rules not applying?**
- Verify file path matches the `paths:` pattern in rule
- Check rule priority

**Permission denied?**
- Check `.claude/settings.json` permissions
- Add exception if needed (consider security)

**Hook not triggering?**
- Check matcher pattern in settings.json
- Verify hook type (prompt, command, block)

## Related Documentation

- [CLAUDE.md](../CLAUDE.md) - Main project instructions
- [MIGRATION.md](../MIGRATION.md) - Migration guide from Copilot
- [.github/DEVELOPMENT.md](../.github/DEVELOPMENT.md) - Development guidelines
- [.github/context/active_state.md](../.github/context/active_state.md) - Current project state

## Questions?

See the Claude Code documentation or ask in project chat.
