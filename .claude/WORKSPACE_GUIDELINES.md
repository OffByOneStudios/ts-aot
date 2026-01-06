# Workspace Guidelines for Claude Code

## Directory Organization Rules

### 1. NO Files in Root Directory

**NEVER create these in the root directory:**
- ❌ Executable files (*.exe)
- ❌ Temporary test files
- ❌ Debug output files
- ❌ IR dump files (*.ll)
- ❌ Object files (*.obj)
- ❌ Scripts for testing
- ❌ Any other temporary artifacts

### 2. Build Artifacts Location

**Test executables should be built in the same directory as the test source:**

**CORRECT:**
```bash
# Compile test in its own directory
./build/src/compiler/Release/ts-aot.exe tests/node/buffer/buffer_basic.ts -o tests/node/buffer/buffer_basic.exe

# Run from test directory
./tests/node/buffer/buffer_basic.exe
```

**INCORRECT:**
```bash
# DON'T build executables in root
./build/src/compiler/Release/ts-aot.exe tests/node/buffer/buffer_basic.ts -o buffer_basic.exe
```

### 3. Temporary Files Location

**Use `tmp/` directory for all temporary files:**

```bash
# Create tmp directory if needed
mkdir -p tmp

# Examples of temporary files:
tmp/debug_output.log
tmp/test_script.sh
tmp/ir_dump.ll
tmp/compile_test.ts
```

### 4. Test Build Directory

**Test build artifacts go in `tests/node/.build/`:**

```bash
# Alternative: Build all test executables in one place
mkdir -p tests/node/.build
./build/src/compiler/Release/ts-aot.exe tests/node/buffer/buffer_basic.ts -o tests/node/.build/buffer_basic.exe
```

## .gitignore Patterns

Ensure these patterns are in `.gitignore`:

```gitignore
# Executables
*.exe
*.out

# Build artifacts
*.obj
*.pdb
*.lib
*.exp
*.ilk

# Temporary files
tmp/
tests/node/.build/
tests/node/**/*.exe
tests/node/**/*.obj
tests/node/**/*.pdb

# IR dumps
*.ll
```

## Workflow Examples

### Testing a Node.js API Test

```bash
# 1. Compile test in its directory
./build/src/compiler/Release/ts-aot.exe \
  tests/node/buffer/buffer_basic.ts \
  -o tests/node/buffer/buffer_basic.exe

# 2. Run from its directory
./tests/node/buffer/buffer_basic.exe

# 3. Clean up (optional - can leave for re-running)
rm tests/node/buffer/buffer_basic.exe
```

### Debugging with IR Dump

```bash
# Dump IR to tmp directory
./build/src/compiler/Release/ts-aot.exe \
  tests/node/buffer/buffer_basic.ts \
  -o tmp/buffer_basic.exe \
  --dump-ir > tmp/buffer_basic.ll
```

### Creating Test Scripts

```bash
# Create script in tmp
cat > tmp/run_tests.sh <<'EOF'
#!/bin/bash
for test in tests/node/buffer/*.ts; do
  name=$(basename "$test" .ts)
  ./build/src/compiler/Release/ts-aot.exe "$test" -o "tests/node/buffer/$name.exe"
  "./tests/node/buffer/$name.exe"
done
EOF

chmod +x tmp/run_tests.sh
./tmp/run_tests.sh
```

## Summary

**Golden Rules:**
1. ✅ Test executables → Same directory as source OR `tests/node/.build/`
2. ✅ Temporary files → `tmp/` directory
3. ✅ Keep root directory clean
4. ✅ Use relative paths from root when running commands
5. ❌ NEVER create artifacts in root directory
