---
name: auto-debug
description: Analyze crashes and access violations using CDB debugger. Use when executable crashes, access violation occurs, or user mentions "crash", "debug", "CDB", "access violation", or "analyze crash".
allowed-tools: Bash, Read, Write
---

# Auto-Debug Skill

Automatically analyze crashes and exceptions in ts-aot compiled executables using the Windows CDB debugger.

## When to Use

**Trigger terms:** crash, access violation, debug executable, analyze crash, CDB, debugger, memory error, segfault

- When an executable crashes without clear error messages
- To analyze crash dumps and get stack traces
- To debug lodash or other complex compilation issues
- When you need automated crash analysis for multiple test cases

## Prerequisites

- Windows SDK Debugging Tools installed (CDB)
- Executable compiled with debug symbols (`-g` flag)
- PDB file present alongside executable

## Instructions

### 1. Identify the Crashing Executable

Look for `.exe` files mentioned in error messages or that the user reports are crashing.

### 2. Run the Debug Analyzer Script

Use the PowerShell script at `.github/skills/auto-debug/debug_analyzer.ps1`:

```powershell
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe
```

**With custom output file:**
```powershell
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe -OutputFile crash_report.txt
```

### 3. Parse the Output

The script captures:
- **Exit Code** - Normal (0) or error code
- **Exception Information** - Type and code of crash (e.g., 0xC0000005 = access violation)
- **Call Stack** - Full stack trace showing crash location
- **Registers** - CPU state at crash
- **Local Variables** - Values at crash point
- **Automatic Analysis** - CDB's `!analyze -v` output
- **Module List** - Loaded DLLs and their versions

### 4. Analyze the Results

Look for:
- **Source Line**: Which line of code crashed
- **Function**: Which function was executing
- **Exception Code**:
  - `0xC0000005` = Access violation (null/invalid pointer)
  - `0xC0000094` = Integer divide by zero
  - `0xC00000FD` = Stack overflow
- **Call Stack**: Trace from crash back to `user_main`

### 5. Report Findings

Provide a clear summary:
```
The executable crashed with an access violation (0xC0000005) in function `add_int_int` at line 42.

Call stack:
  user_main+0x142
  ts_main+0x89
  add_int_int+0x23  <-- CRASH HERE

The crash appears to be caused by dereferencing a null pointer when accessing array element.
```

### 6. Suggest Fixes

Based on the crash type and location, suggest:
- Null pointer checks
- Array bounds validation
- Type casting issues (check runtime-safety.md rules)
- Boxing/unboxing problems

## Example Usage

```powershell
# User reports: "examples/test.exe crashes with access violation"

# Step 1: Run debug analyzer
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe

# Step 2: Read the output file
# (The script will display a summary and save full output to debug_analysis.txt)

# Step 3: Analyze the stack trace
# Look for the function and line number where the crash occurred

# Step 4: Identify the root cause
# Example: "Crash in ts_array_get at null pointer dereference"

# Step 5: Suggest fix
# "The array pointer is null. Check that array is properly initialized before calling .get()"
```

## Integration with Workflow

### After Compilation

```powershell
# Compile with debug symbols
build\src\compiler\Release\ts-aot.exe examples\test.ts -o examples\test.exe -g

# Run the executable
examples\test.exe

# If it crashes, analyze
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe
```

### Batch Testing

```powershell
# Test multiple files and collect crash data
$tests = @("test1.exe", "test2.exe", "test3.exe")
foreach ($test in $tests) {
    Write-Host "`nAnalyzing $test..."
    .\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath "examples\$test" -OutputFile "$test.debug.txt"
}
```

## Troubleshooting

### "CDB not found"
Install Windows SDK from: https://developer.microsoft.com/windows/downloads/windows-sdk/
Select "Debugging Tools for Windows" during installation.

### "No symbols found"
Ensure:
- Executable compiled with `-g` flag
- PDB file exists alongside .exe
- Symbol paths are correct

### "Access denied"
Run PowerShell as Administrator if debugging system processes.

## Output Format

The skill produces two outputs:

1. **Console Summary** - Key crash details highlighted
2. **Full Report** - Complete debug output in `debug_analysis.txt` (or custom file)

## Common Crash Patterns

### Access Violation (0xC0000005)

**Typical causes:**
- Null pointer dereference
- Invalid object cast (check runtime-safety.md)
- Corrupted this pointer (virtual inheritance casting issue)
- Buffer overflow

**What to check:**
- Verify all `dynamic_cast` usage
- Check for C-style casts in runtime code
- Ensure proper unboxing of void* parameters

### Integer Divide by Zero (0xC0000094)

**Typical causes:**
- Division or modulo with zero divisor
- Missing validation in math operations

### Stack Overflow (0xC00000FD)

**Typical causes:**
- Infinite recursion
- Very large stack allocations
- Excessive function call depth

## Advanced Features

### Custom CDB Commands

Edit `debug_analyzer.ps1` to add custom commands:

```powershell
$commands = @(
    "g"                    # Run
    "k"                    # Stack
    "dv"                   # Variables
    "!heap"                # Heap analysis
    "!locks"               # Lock analysis
    ".dump /ma crash.dmp"  # Create dump file
    "q"                    # Quit
)
```

### Parsing Output Programmatically

The script returns a PowerShell object:

```powershell
$result = .\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath test.exe
if ($result.HasCrash) {
    Write-Host "Crash detected! See $($result.OutputFile)"
}
```

## Important Notes

- **NEVER invoke `cdb` directly** - always use the skill script
- **ALWAYS compile with `-g` flag** for debug symbols
- **Check runtime-safety.md** for common memory safety issues
- Full debug output is saved to `debug_analysis.txt` by default
- The script automatically handles CDB paths and output parsing

## Related Documentation

- @.github/DEVELOPMENT.md - Debugging & Diagnostics section
- @.claude/rules/runtime-safety.md - Memory safety patterns
- @.github/skills/auto-debug/debug_analyzer.ps1 - The actual script
