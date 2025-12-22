---
name: ctags-search
description: Search for code symbols (classes, functions, variables) using the project's ctags index. Use this when you need to find definitions or locate specific symbols across the codebase.
---

# Ctags Search Skill

This skill allows you to perform fast, indexed searches for symbols in the `ts-aot` codebase using `universal-ctags`.

## When to use
- When you need to find the definition of a class, function, or variable.
- When you want to see all occurrences of a symbol name in the index.
- When `grep_search` or `semantic_search` are too slow or return too much noise.

## Procedures

### Search for a symbol
To search for a symbol, run the `search_tags.ps1` script in the terminal.

```powershell
powershell -ExecutionPolicy Bypass -File .github/skills/ctags-search/search_tags.ps1 -Query "SymbolName"
```

### Exact match search
If you know the exact name of the symbol and want to find its definition:

```powershell
powershell -ExecutionPolicy Bypass -File .github/skills/ctags-search/search_tags.ps1 -Query "SymbolName" -Exact
```

## Guidelines
- The `tags` file is located in the root of the workspace.
- If the `tags` file is missing, instruct the user to run the "update tags" task.
- Prefer this skill over `grep_search` for finding symbol definitions.
