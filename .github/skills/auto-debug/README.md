# Auto-Debug Skill

Automated crash analysis for ts-aot compiled executables.

## Quick Start

```powershell
# Analyze a crash
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath examples\debug_test.exe
```

## Features

✅ **Automated CDB Execution** - Runs debugger without manual interaction  
✅ **Crash Detection** - Identifies exceptions and crashes  
✅ **Stack Trace Extraction** - Shows call stack at crash point  
✅ **Variable Inspection** - Captures local variables  
✅ **Smart Parsing** - Highlights relevant information  
✅ **Batch Processing** - Debug multiple executables  
✅ **CI/CD Ready** - Use in automated pipelines  

## How It Works

1. **Launches CDB** with pre-configured command script
2. **Runs executable** until crash or exit
3. **Captures output** including stack, registers, variables
4. **Parses results** and extracts key information
5. **Generates report** with summary and full details

## Example Output

```
=== Summary ===
Exit Code: 1
Exception Code: 0xC0000005
Call Stack (Top 10):
  user_main+0x142
  add_int_int+0x23
Source Line: 42
```

## Documentation

See [skill.md](skill.md) for complete documentation.

## Requirements

- Windows SDK Debugging Tools (CDB)
- PowerShell 5.1+
- Debug symbols (compile with `-g`)
