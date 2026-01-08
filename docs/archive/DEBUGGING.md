# Debugging ts-aot Executables

## Quick Start

```powershell
# Run with crash detection
.\debug.ps1 examples\debug_test.exe

# Open in Visual Studio debugger
.\debug.ps1 examples\debug_test.exe -BreakOnStart
```

## Option 1: Visual Studio GUI Debugger (Installed)

```powershell
# Method 1: Using helper script
.\debug.ps1 examples\debug_test.exe -BreakOnStart

# Method 2: Direct command
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe" /debugexe examples\debug_test.exe
```

**Advantages:**
- Full GUI with source view, watch windows, call stack
- Can set breakpoints, step through code
- Best for interactive debugging

## Option 2: Windows SDK Debuggers (Not Installed)

Install from: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

After installation, you'll have:

### CDB (Console Debugger)
```powershell
# Run until crash
cdb examples\debug_test.exe

# Common commands:
#   g     - go (continue execution)
#   bp    - set breakpoint
#   k     - show call stack
#   dt    - display type
#   dv    - display local variables
#   q     - quit
```

### WinDbg (GUI with command line)
```powershell
windbg examples\debug_test.exe
```

## Option 3: Post-Mortem Debugging

Enable automatic crash dumps:

```powershell
# Create crash dump directory
New-Item -Path "E:\src\github.com\cgrinker\ts-aoc\dumps" -ItemType Directory -Force

# Enable Windows Error Reporting dumps (requires admin)
# reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /v DumpFolder /t REG_EXPAND_SZ /d "E:\src\github.com\cgrinker\ts-aoc\dumps" /f
# reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps" /v DumpType /t REG_DWORD /d 2 /f
```

Then analyze dumps with:
```powershell
cdb -z dumps\debug_test.exe.*.dmp
```

## Option 4: Using Visual Studio Code (If installed)

Create `.vscode/launch.json`:
```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug ts-aot executable",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "${workspaceFolder}/examples/debug_test.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "console": "integratedTerminal"
        }
    ]
}
```

## Debugging Tips

### Check for Debug Symbols
```powershell
# PDB should exist for executables compiled with -g
Test-Path examples\debug_test.pdb  # Should be True
```

### View Debug Info in LLVM IR
```powershell
# After compiling with -g, check debug_ir.ll
Select-String -Path debug_ir.ll -Pattern "DISubprogram|DILocalVariable" | Select-Object -First 20
```

### Quick Test
```powershell
# Compile with debug symbols
build\src\compiler\Release\ts-aot.exe examples\debug_test.ts -o examples\debug_test.exe -g

# Run with helper script
.\debug.ps1 examples\debug_test.exe
```

## Current Setup

✅ Visual Studio 2022 Community installed  
❌ Windows SDK Debuggers (CDB/WinDbg) not installed  
✅ Debug symbols working (DWARF/CodeView metadata in LLVM IR)  

**Recommended:** Install Windows SDK for command-line debugging with CDB.
