# Node.js API Test Runner

Automated test harness for running all Node.js API tests in the `tests/node/` directory.

## Usage

### PowerShell (Windows - Recommended)
```powershell
cd tests/node
powershell -ExecutionPolicy Bypass -File run_tests.ps1
```

### Python (Cross-platform)
```bash
cd tests/node
python run_tests.py
```

With verbose output:
```bash
python run_tests.py -v
```

## Test Status Categories

- **PASS** ✅ - Test compiled and ran successfully (exit code 0)
- **FAIL** ❌ - Test compiled but failed (non-zero exit code)
- **BLOCKED** 🟡 - Test compiled but is blocked by runtime bugs (async, event loop, etc.)
- **SKIPPED** 🔵 - Test has skipped assertions due to known implementation bugs
- **COMPILE ERR** 🔴 - Test failed to compile

## Current Test Coverage

### Working Tests (Synchronous)
- **Buffer API** - 4 test files (52 tests)
  - buffer_basic.ts - Basic buffer operations
  - buffer_advanced.ts - Advanced buffer operations (29 tests)
  - buffer_encoding.ts - Encoding/decoding (14 tests)
  - buffer_typed_array.ts - TypedArray compatibility (6 tests)

- **Filesystem API** - 6 test files (~50 tests)
  - fs_sync.ts - Synchronous file operations
  - fs_async.ts - Async file operations (blocked)
  - fs_metadata.ts - stat, fstat, access operations (9 tests)
  - fs_dirs.ts - Directory operations (10 tests)
  - fs_links.ts - Hard links and symlinks (7 tests)
  - fs_operations.ts - File manipulation (7 tests)

- **Path API** - 1 test file (19 tests)
  - path_basic.ts - All path module methods

- **Crypto API** - 1 test file (3 tests)
  - crypto_basic.ts - crypto.md5 tests

- **URL API** - 1 test file (8 tests)
  - url_basic.ts - URL constructor and parsing

- **Util API** - 1 test file (10 tests)
  - util_basic.ts - util.format, inspect, types

- **JSON API** - 1 test file (16 tests)
  - json_basic.ts - JSON.stringify and JSON.parse

- **Object API** - 1 test file (2 tests)
  - object_basic.ts - Object.keys (limited by compiler bug)

### Blocked Tests (Async Runtime Required)
- **Promise API** - 2 test files (15 tests)
  - promises_basic.ts - Promise.resolve, reject, then, catch
  - promises_static.ts - Promise.all, race, allSettled, any

- **Async/Await** - 2 test files (16 tests)
  - async_basic.ts - Basic async/await operations
  - async_error.ts - Async error handling

- **Events API** - 1 test file (10 tests)
  - events_basic.ts - EventEmitter tests

- **HTTP/Network** - 3 test files (18 tests)
  - http_client.ts - HTTP client requests
  - https_basic.ts - HTTPS client requests
  - fetch_basic.ts - Fetch API

- **Process API** - 2 test files
  - process_basic.ts - Basic process properties (blocked by runtime bug)
  - process_extended.ts - Extended process APIs (blocked by runtime bug)

- **Timers API** - 1 test file
  - timers_basic.ts - setTimeout, setInterval (blocked by event loop)

## Known Issues

### Compiler Bugs
1. **Multiple Object.* calls** - Using multiple Object API methods in one file causes segfault
2. **Process module exit 127** - Process properties cause runtime crash
3. **Array access bug** - process.argv and other array accesses can crash

### Runtime Bugs
1. **Async/await state machine** - Doesn't resume after await
2. **Event loop** - Timer callbacks never fire, events don't work
3. **Stats API** - isFile() and isDirectory() both return true
4. **fstatSync** - Returns size 0
5. **writeSync/readSync** - Wrong byte count, position parameter doesn't work
6. **realpathSync** - Causes exit 127 crash
7. **JSON implementation** - Various bugs with parse/stringify edge cases
8. **Crypto.md5** - Returns "undefined" string instead of hash

## Output Format

The test runner shows:
- Status (PASS/FAIL/BLOCKED/SKIPPED/COMPILE ERR)
- Test name (category/filename.ts)
- Error details for failures (when verbose or on failure)

Final summary includes:
- Total test count
- Pass/fail/blocked/skipped counts
- Compile error count
- Pass rate (excluding blocked/skipped tests)

## Exit Codes

- **0** - All tests passed (or only blocked/skipped)
- **1** - One or more tests failed or had compile errors

## Adding New Tests

1. Create a `.ts` file in the appropriate category directory under `tests/node/`
2. Follow the test template pattern:
   - Export `function user_main(): number` (synchronous) or `async function user_main(): Promise<number>` (async)
   - Return 0 on success, non-zero on failure
   - Use console.log for PASS/FAIL messages
   - Include summary with failure count

3. Run the test harness to verify

## Test Template

See `tests/node/test_template.ts` for detailed templates for:
- Synchronous tests
- Async tests
- Event-driven tests
- Error handling patterns
