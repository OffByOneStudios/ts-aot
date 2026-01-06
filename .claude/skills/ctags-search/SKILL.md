---
name: ctags-search
description: Search for symbol definitions using ctags. Use when looking for function/class/method definitions, or user mentions "find symbol", "search definition", "where is defined", "ctags", or "locate function".
allowed-tools: Bash, Read, Grep
---

# CTag Search Skill

Search for symbol definitions (functions, classes, methods, structs) using ctags. Preferred over grep for finding where symbols are defined.

## When to Use

**Trigger terms:** find symbol, search definition, where is defined, locate function, find class, ctags, symbol lookup

- Looking for where a function is defined
- Finding class or struct definitions
- Locating method implementations
- Searching across the codebase for symbol definitions
- When grep would give too many results (declarations vs definitions)

## Prerequisites

- Universal ctags or exuberant ctags installed
- PowerShell (on Windows)
- Access to project source files

## Instructions

### 1. Identify the Symbol to Search

Get the symbol name from:
- User's question ("Where is `ts_array_get` defined?")
- Error message
- Code reference
- Stack trace

### 2. Run the Search Script

Use the PowerShell script at `.github/skills/ctags-search/search_tags.ps1`:

```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_get"
```

**With specific file types:**
```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_get" -FileTypes "*.cpp,*.h"
```

**With custom search path:**
```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "TsArray" -SearchPath "src/runtime"
```

### 3. Parse the Output

The script returns:
- **File path**: Where the symbol is defined
- **Line number**: Exact line of definition
- **Symbol type**: Function, class, struct, method, etc.
- **Signature**: Function signature or class declaration

Example output:
```
Found 3 matches for 'ts_array_get':

[Function] ts_array_get
  File: src/runtime/src/TsArray.cpp:145
  Signature: void* ts_array_get(void* ctx, void* arr, int64_t index)

[Function] __ts_array_get_inline
  File: src/compiler/codegen/IRGenerator_Expressions_Access.cpp:234
  Signature: llvm::Value* __ts_array_get_inline(llvm::Value* arr, llvm::Value* index)

[Method] TsArray::get
  File: src/runtime/include/TsArray.h:28
  Declaration: void* get(int64_t index);
```

### 4. Navigate to Definition

Open the file at the specified line number:

```markdown
The function `ts_array_get` is defined in [src/runtime/src/TsArray.cpp:145](src/runtime/src/TsArray.cpp#L145)
```

### 5. Read the Definition

Use the Read tool to examine the code:

```
Read src/runtime/src/TsArray.cpp starting at line 140 to see the implementation.
```

## Search Patterns

### Find Function Definition

```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "visitExpression"
```

Returns all functions named `visitExpression` with their locations.

### Find Class Definition

```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "TsArray"
```

Returns the class definition with file and line number.

### Find Method Implementation

```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "TsArray::push"
```

Returns the specific method implementation.

### Find in Specific Directory

```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "boxValue" -SearchPath "src/compiler/codegen"
```

Limits search to codegen directory.

## Symbol Types

The skill identifies:
- **Function** (f): Global functions, extern "C" functions
- **Class** (c): Class definitions
- **Struct** (s): Struct definitions
- **Method** (m): Class member functions
- **Member** (p): Class member variables
- **Enum** (g): Enum definitions
- **Typedef** (t): Type aliases
- **Namespace** (n): Namespace definitions
- **Macro** (d): #define macros

## Advantages Over Grep

### Grep Shows Everything
```bash
grep -r "visitExpression"
# Returns 500+ results: declarations, calls, comments, etc.
```

### CTag Shows Definitions Only
```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "visitExpression"
# Returns 10 results: only where visitExpression is defined
```

## Common Use Cases

### 1. User Asks "Where is X defined?"

```powershell
# User: "Where is ts_value_get_object defined?"
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_value_get_object"

# Response: "It's defined in src/runtime/src/TsObject.cpp:89"
```

### 2. Need to Understand Function Signature

```powershell
# Find the signature before calling it
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "createCall"

# Read the definition to see parameters
```

### 3. Find All Overloads

```powershell
# Find all visitExpression overloads
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "visitExpression"

# Shows: visitExpression in Analyzer, IRGenerator, etc.
```

### 4. Locate Runtime Function Implementation

```powershell
# User reports bug in ts_array_push
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_push"

# Find the implementation, then Read the file
```

## Integration with Workflow

### During Code Review

```powershell
# Check where a new function is defined
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "newFunctionName"

# Verify it's in the right file
```

### During Debugging

```powershell
# Stack trace shows crash in "add_int_int"
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "add_int_int"

# Find the function, read it, analyze the bug
```

### When Adding Features

```powershell
# Find similar existing functions
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_map"

# Use as template for new function
```

## Limitations

### Does Not Find

- **Variable references**: Use grep for this
- **Call sites**: Use grep to find where a function is called
- **String literals**: Use grep for text search
- **Comments**: Not indexed by ctags

### May Miss

- **Template instantiations**: Ctags may not index all specializations
- **Lambda functions**: Usually not indexed
- **Inline functions**: May show declaration, not inline definition

## Fallback to Grep

When ctags doesn't find the symbol, fall back to grep:

```bash
# Ctags found nothing, try grep
grep -r "mySymbol" src/
```

## Troubleshooting

### "Ctags not found"

Install Universal Ctags:
- Windows: `choco install universal-ctags`
- Linux: `apt-get install universal-ctags`
- Mac: `brew install universal-ctags`

### "No tags found"

The script generates tags on the fly, but if it fails:
```powershell
# Manually generate tags
ctags -R --languages=C++ --c++-kinds=+p --fields=+iaS --extra=+q src/
```

### "Too many results"

Narrow the search:
```powershell
# Search only in runtime
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "get" -SearchPath "src/runtime"

# Search only .cpp files
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "get" -FileTypes "*.cpp"
```

### "Symbol not found"

Check:
- Spelling of symbol name
- Symbol may be in a different case (`ts_array_get` vs `TS_ARRAY_GET`)
- Symbol may be a macro (try grep instead)
- Symbol may be in a file not indexed (check FileTypes)

## Advanced Usage

### Search Multiple Symbols

```powershell
$symbols = @("ts_array_get", "ts_array_set", "ts_array_push")
foreach ($sym in $symbols) {
    .\.github\skills\ctags-search\search_tags.ps1 -SymbolName $sym
}
```

### Combine with Grep

```powershell
# Find definition
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_get"

# Find all call sites
grep -r "ts_array_get" src/compiler/
```

### Export Results

```powershell
.\.github\skills\ctags-search\search_tags.ps1 -SymbolName "ts_array_get" > search_results.txt
```

## Output Format

The script provides structured output:

```
Found N matches for 'symbol_name':

[Type] SymbolName
  File: path/to/file.cpp:123
  Signature: return_type symbol_name(param1, param2)
  Scope: ClassName::MethodName (if applicable)
```

## Related Documentation

- @.github/copilot-instructions.md - "Use ctags-search skill for symbol lookups"
- @.github/skills/ctags-search/search_tags.ps1 - The actual script
- Universal Ctags: https://ctags.io/
