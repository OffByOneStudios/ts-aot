# GitHub Copilot → Claude Code Migration Guide

This document explains the migration from GitHub Copilot to Claude Code for the ts-aot project.

## Summary of Changes

### Files Created

**Core Configuration:**
- `CLAUDE.md` - Main project instructions (replaces `.github/copilot-instructions.md`)
- `.claude/settings.json` - Permissions, hooks, and project settings
- `.gitignore` - Updated to exclude local Claude settings

**Modular Rules (in `.claude/rules/`):**
- `runtime-safety.md` - Critical memory safety rules for `src/runtime/**`
- `llvm-ir-patterns.md` - LLVM 18 IR generation patterns for `src/compiler/codegen/**`
- `typescript-conventions.md` - TypeScript test file conventions for `examples/**`

**Skills (in `.claude/skills/`):**
- `auto-debug/SKILL.md` - CDB crash analysis (migrated from `.github/skills/auto-debug/`)
- `golden-ir-tests/SKILL.md` - Golden IR test runner (new)
- `ctags-search/SKILL.md` - Symbol definition search (migrated from `.github/skills/ctags-search/`)

### Migration Philosophy

**What Changed:**
- Instructions moved from `.github/copilot-instructions.md` to `CLAUDE.md`
- Modular rules split into separate files by domain (runtime, codegen, tests)
- Skills converted to YAML-frontmatter format with trigger terms
- Added permissions and hooks for safety and automation

**What Stayed the Same:**
- All original `.github/` documentation preserved for reference
- Original skill scripts (`.ps1` files) unchanged and still functional
- Development workflow and technical constraints unchanged
- Documentation in `docs/` untouched

## Key Differences from Copilot

### 1. Automatic Context Loading

**Copilot:** Required manual reading of instructions file
**Claude Code:** Automatically loads `CLAUDE.md` at session start

### 2. Modular Rules with Path Matching

**Copilot:** All rules in one file
**Claude Code:** Rules in `.claude/rules/` auto-apply based on file paths

Example: Editing `src/runtime/TsArray.cpp` automatically triggers `runtime-safety.md` rules.

### 3. Skills with Automatic Activation

**Copilot:** User had to remember to use skills
**Claude Code:** Skills activate based on description and trigger terms

Example: Mentioning "crash" or "debug" automatically suggests the auto-debug skill.

### 4. Permission System

**Copilot:** No built-in permission controls
**Claude Code:** Fine-grained tool permissions in `.claude/settings.json`

Example: Blocks direct `cdb` invocation, requires using the auto-debug skill script.

### 5. Hooks for Automation

**Copilot:** No hooks
**Claude Code:** Hooks trigger on events (SessionStart, PostToolUse, PreToolUse)

Example: After editing runtime code, shows reminder to check safety rules.

## Using the New System

### Starting a Session

When you start Claude Code, it will:
1. Load `CLAUDE.md` automatically
2. Show reminder to read `.github/context/active_state.md`
3. Make all skills available for use

### Editing Runtime Code

When editing files in `src/runtime/`:
1. Claude Code applies `runtime-safety.md` rules automatically
2. After editing, you'll see a safety reminder
3. The rules prevent common memory safety errors

### Running Skills

**Auto-Debug:**
- Just mention "crash" or "access violation"
- Claude will suggest using the auto-debug skill
- Or explicitly request: "Use the auto-debug skill on examples/test.exe"

**Golden IR Tests:**
- Say "run the golden tests" or "check regression tests"
- Claude will use the skill to run the test suite

**CTag Search:**
- Ask "where is ts_array_get defined?"
- Claude will use ctags-search to find it

### Permissions

The system blocks:
- Direct CDB invocation (use auto-debug skill instead)
- Destructive operations (`rm`, `del`)
- Network operations (`curl`, `wget`)
- Editing critical files (CMakeLists.txt, workflow files)
- Reading sensitive files (.env, secrets/)

The system allows:
- Git operations
- Build operations (cmake, make)
- Python and Node.js scripts
- Reading source files
- Editing source files

## File Structure

```
your-project/
├── CLAUDE.md                      # Main instructions (was .github/copilot-instructions.md)
├── .claude/
│   ├── settings.json              # Permissions and hooks
│   ├── rules/
│   │   ├── runtime-safety.md      # Auto-applied to src/runtime/**
│   │   ├── llvm-ir-patterns.md    # Auto-applied to src/compiler/codegen/**
│   │   └── typescript-conventions.md  # Auto-applied to examples/**
│   └── skills/
│       ├── auto-debug/
│       │   └── SKILL.md           # Crash analysis skill
│       ├── golden-ir-tests/
│       │   └── SKILL.md           # Test runner skill
│       └── ctags-search/
│           └── SKILL.md           # Symbol search skill
├── .github/                       # Original Copilot files (preserved for reference)
│   ├── copilot-instructions.md    # Original instructions
│   ├── DEVELOPMENT.md
│   ├── context/
│   ├── instructions/
│   └── skills/                    # Original skill scripts (.ps1 files)
└── docs/                          # Phase documentation
```

## What to Commit

**Commit to git:**
- `CLAUDE.md`
- `.claude/settings.json`
- `.claude/rules/*.md`
- `.claude/skills/*/SKILL.md`
- Updated `.gitignore`
- This `MIGRATION.md`

**Don't commit (gitignored):**
- `.claude/CLAUDE.local.md` - Personal preferences
- `.claude/settings.local.json` - Personal settings
- `.claude/**/*.local.*` - Any local files

## Team Collaboration

When team members clone the repo:
1. They get `CLAUDE.md` and all rules automatically
2. Skills are available immediately
3. Permissions and hooks are enforced
4. They can create `.claude/CLAUDE.local.md` for personal preferences

## Backwards Compatibility

The original `.github/` structure is preserved:
- `.github/copilot-instructions.md` - Still there for reference
- `.github/skills/` - Scripts still work as before
- `.github/context/` - Context files unchanged
- `.github/instructions/` - Code snippets still available

You can continue using GitHub Copilot alongside Claude Code if desired.

## Next Steps

1. **Test the migration:**
   - Start a new Claude Code session
   - Verify `CLAUDE.md` loads automatically
   - Try editing a runtime file and check the safety reminder
   - Test a skill (e.g., "find where ts_array_get is defined")

2. **Customize if needed:**
   - Add personal preferences to `.claude/CLAUDE.local.md`
   - Adjust permissions in `.claude/settings.local.json`
   - Add more rules or skills as the project grows

3. **Share with team:**
   - Commit the new structure to git
   - Update team documentation
   - Explain the new system in team meeting

## Troubleshooting

**"Skills not activating"**
- Check skill descriptions include relevant trigger terms
- Explicitly mention the skill: "Use the auto-debug skill"

**"Rules not applying"**
- Verify file path matches the `paths:` pattern in the rule
- Check rule priority (critical > high > medium > low)

**"Permission denied"**
- Check `.claude/settings.json` permissions
- Add exception if needed (but consider security first)

**"Old Copilot instructions showing"**
- This is OK - Claude Code loads `CLAUDE.md`, Copilot loads `.github/copilot-instructions.md`
- They can coexist

## Questions?

See the Claude Code documentation or ask in the project chat.

---

**Migration completed:** 2026-01-06
**Original Copilot setup:** Preserved in `.github/`
**Claude Code setup:** `.claude/` and `CLAUDE.md`
