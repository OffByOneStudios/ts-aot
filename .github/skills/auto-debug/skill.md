# Auto-Debug Skill

**Purpose:** Automatically analyze crashes and exceptions in ts-aot compiled executables using CDB debugger.

## When to Use This Skill

- When an executable crashes without clear error messages
- To analyze crash dumps and get stack traces
- To debug lodash or other complex compilation issues
- When you need automated crash analysis for multiple test cases

## Prerequisites

- Windows SDK Debugging Tools installed (CDB)
- Executable compiled with debug symbols (`-g` flag)
- PDB file present alongside executable

## Usage

### Basic Analysis

```powershell
# Analyze a crash
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\debug_test.exe

# Save to custom output file
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\debug_test.exe -OutputFile crash_report.txt
```

### From Copilot Chat

You can invoke this skill by asking:

- "Debug the crash in [file]"
- "Analyze the crash in examples/test.exe"
- "Run auto-debug on the failing executable"
- "Get crash information for [program]"

## What It Captures

1. **Exit Code** - Normal (0) or error code
2. **Exception Information** - Type and code of crash
3. **Call Stack** - Full stack trace showing crash location
4. **Registers** - CPU state at crash
5. **Local Variables** - Values at crash point
6. **Automatic Analysis** - CDB's `!analyze -v` output
7. **Module List** - Loaded DLLs and their versions

## Output Format

The skill produces two outputs:

1. **Console Summary** - Key crash details highlighted
2. **Full Report** - Complete debug output in `debug_analysis.txt`

### Console Summary Example

```
=== Automated Debug Analysis ===
Executable: examples\test.exe
Debugger: C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe

Running debugger with automated commands...

=== Debug Analysis Complete ===
Full output saved to: debug_analysis.txt

=== Summary ===

Exit Code: 1
Exception Code: 0xC0000005
Call Stack (Top 10):
  00 ntdll!RtlUserThreadStart
  01 KERNEL32!BaseThreadInitThunk
  02 user_main+0x142
  03 ts_main+0x89
  04 add_int_int+0x23

Source Line: 42
Function: add_int_int

Error Messages:
  Access violation - code c0000005
```

## Integration with Development Workflow

### 1. After Compilation Failure

```powershell
# Compile with debug symbols
build\src\compiler\Release\ts-aot.exe examples\test.ts -o examples\test.exe -g

# If it crashes, analyze
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\test.exe
```

### 2. Batch Testing

```powershell
# Test multiple files and collect crash data
$tests = @("test1.exe", "test2.exe", "test3.exe")
foreach ($test in $tests) {
    Write-Host "`nAnalyzing $test..."
    .\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath "examples\$test" -OutputFile "$test.debug.txt"
}
```

### 3. With CI/CD

```yaml
# GitHub Actions example
- name: Debug failed tests
  if: failure()
  run: |
    .\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath ${{ env.FAILED_TEST }}
```

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

### Parsing Custom Output

The script returns a PowerShell object for programmatic use:

```powershell
$result = .\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath test.exe
if ($result.HasCrash) {
    Write-Host "Crash detected! See $($result.OutputFile)"
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

## Future Enhancements

- [ ] Automated source code correlation
- [ ] Crash pattern detection across multiple runs
- [ ] Integration with lodash compilation debugging
- [ ] Visual report generation
- [ ] Memory leak detection
- [ ] Performance profiling integration
