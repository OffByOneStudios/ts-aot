---
name: auto-debug
description: Analyze crashes and access violations using CDB debugger. Use when executable crashes, access violation occurs, or user mentions "crash", "debug", "CDB", "access violation", or "analyze crash".
allowed-tools: Bash, Read, Write
---

# Auto-Debug Skill

Analyze crashes in ts-aot compiled executables using the x64 CDB debugger (on system PATH).

## When to Use

**Trigger terms:** crash, access violation, debug, analyze crash, CDB, debugger, segfault

## Prerequisites

- CDB is on the system PATH (`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb`)
- For source-level debugging: compile with `-g` flag to generate PDB files
- PDB will be auto-generated alongside the .exe

## Debug Symbol Info

The compiler generates CodeView/PDB debug info when `-g` is passed:
- Source file + line number mapping (TypeScript/JavaScript → LLVM IR → native)
- Function names (including mangled names like `selectColor_m310916`)
- DISubprogram entries for each compiled function
- Note: `!dbg` verification warnings may occur (non-fatal, PDB still generated)

## Quick CDB Commands

### Get a stack trace from a crashing executable

```bash
# Compile with debug symbols
build/src/compiler/Release/ts-aot.exe mytest.ts -g -o tmp/mytest.exe

# Run CDB with stack trace on first access violation
printf "sxe av\ng\nkb 20\nq\n" > tmp/cdb_cmds.txt
cdb -y "E:\src\github.com\cgrinker\ts-aoc\tmp" -cf tmp/cdb_cmds.txt "E:\src\github.com\cgrinker\ts-aoc\tmp\mytest.exe" 2>&1 | grep -A 20 "c0000005"
```

### Get function names at crash addresses

```bash
printf "sxe av\ng\nkn 20\nq\n" > tmp/cdb_cmds.txt
cdb -y "<pdb_dir>" -cf tmp/cdb_cmds.txt "<full_path_to_exe>"
```

### Key CDB commands
- `kn 20` — stack with frame numbers (needs PDB for symbol names)
- `kb 20` — stack with first 3 args
- `kP 20` — stack with full parameters
- `r` — registers
- `ln <addr>` — list nearest symbols for address
- `lsa .` — list source lines at current IP
- `dv /t` — display local variables with types
- `!analyze -v` — automatic crash analysis
- `sxe av` — break on access violation (first-chance)
- `sxn av` — notify but don't break on first-chance AV

### Use the PowerShell script

```bash
powershell -ExecutionPolicy Bypass -File .github/skills/auto-debug/debug_analyzer.ps1 -ExePath tmp/mytest.exe
```

## Important Notes

- CDB requires **full Windows paths** for executables (not relative Unix-style)
- Without PDB (`-g`), stack traces show offsets only (e.g., `test+0xafd4`)
- With PDB, stack traces show function names (e.g., `test!ts_object_keys+0x44`)
- The VectoredException handler in the runtime catches first-chance AVs — use `sxe av` to break before the handler runs
- Common crash patterns:
  - `VCRUNTIME140!_RTDynamicCast` — dynamic_cast on non-TsObject type (TsString, TsArray)
  - `TsArray::Length` at `[rcx+10h]` — NaN-boxed value passed as array pointer (need unboxing)
  - `reading address FFFFFFFFFFFFFFFF` — vtable/RTTI corruption, or -1 sentinel value
  - `reading address 0x0000000A` — small integer passed as pointer (NaN-boxed int)
