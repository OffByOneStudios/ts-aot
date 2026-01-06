# Node.js API Test Suite

This directory contains comprehensive tests for Node.js API compatibility in ts-aot.

## Organization

Tests are organized by Node.js module to align with the [compatibility matrix](../../docs/phase18/compatibility_matrix.md):

```
tests/node/
├── fs/          # File system API tests
├── path/        # Path module tests
├── crypto/      # Cryptography tests
├── buffer/      # Buffer API tests
├── url/         # URL and fetch tests
├── http/        # HTTP/HTTPS tests
├── net/         # TCP networking tests
├── timers/      # setTimeout/setInterval tests
├── promises/    # Promise API tests
├── process/     # Process globals tests
├── streams/     # Stream API tests
├── events/      # EventEmitter tests
└── util/        # Utility module tests
```

## Test File Naming Conventions

**Format:** `<module>_<feature>.ts`

**Examples:**
- `fs/fs_sync.ts` - Synchronous file system operations
- `http/http_server.ts` - HTTP server functionality
- `buffer/buffer_encoding.ts` - Buffer encoding/decoding

**For comprehensive tests:**
- `<module>_basic.ts` - Basic functionality
- `<module>_advanced.ts` - Advanced features
- `<module>_edge_cases.ts` - Edge cases and error handling

## Test Structure

### Standard Test Pattern

```typescript
// tests/node/fs/fs_sync.ts

import * as fs from 'fs';
import * as path from 'path';

function user_main(): number {
  let failures = 0;

  // Test 1: Basic functionality
  try {
    fs.writeFileSync('test.txt', 'Hello, World!');
    const content = fs.readFileSync('test.txt', 'utf8');

    if (content !== 'Hello, World!') {
      console.log('FAIL: Content mismatch');
      failures++;
    } else {
      console.log('PASS: fs.readFileSync/writeFileSync');
    }

    fs.unlinkSync('test.txt');
  } catch (e) {
    console.log('FAIL: Exception thrown:', e);
    failures++;
  }

  // Test 2: Error cases
  try {
    fs.readFileSync('nonexistent.txt');
    console.log('FAIL: Should have thrown error');
    failures++;
  } catch (e) {
    console.log('PASS: Error handling');
  }

  return failures;
}
```

### Async Test Pattern

```typescript
// tests/node/fs/fs_async.ts

import * as fs from 'fs';

async function runTests(): Promise<number> {
  let failures = 0;

  // Test async operations
  try {
    await fs.promises.writeFile('test.txt', 'Async content');
    const content = await fs.promises.readFile('test.txt', 'utf8');

    if (content !== 'Async content') {
      console.log('FAIL: Async content mismatch');
      failures++;
    } else {
      console.log('PASS: fs.promises.readFile/writeFile');
    }

    await fs.promises.unlink('test.txt');
  } catch (e) {
    console.log('FAIL: Async exception:', e);
    failures++;
  }

  return failures;
}

async function user_main(): Promise<number> {
  const failures = await runTests();
  return failures;
}
```

### Server Test Pattern

For tests that require a server process (HTTP, HTTPS, TCP):

```typescript
// tests/node/http/http_server.ts
// SERVER-TEST: true
// TIMEOUT: 5000

import * as http from 'http';

function user_main(): number {
  return new Promise<number>((resolve) => {
    let failures = 0;

    // Create server
    const server = http.createServer((req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('Hello from server');
    });

    // Start server on random port
    server.listen(0, () => {
      const addr = server.address();
      const port = addr.port;

      // Self-test: make request to own server
      http.get(`http://localhost:${port}`, (res) => {
        let data = '';
        res.on('data', (chunk) => { data += chunk; });
        res.on('end', () => {
          if (data !== 'Hello from server') {
            console.log('FAIL: Response mismatch');
            failures++;
          } else {
            console.log('PASS: HTTP server/client');
          }

          // Shutdown server
          server.close(() => {
            resolve(failures);
          });
        });
      }).on('error', (e) => {
        console.log('FAIL: Request error:', e);
        failures++;
        server.close(() => resolve(failures));
      });
    });

    server.on('error', (e) => {
      console.log('FAIL: Server error:', e);
      resolve(1);
    });
  });
}
```

## Writing Good Tests

### DO:
- ✅ Test one feature or related group of features per file
- ✅ Include both success and error cases
- ✅ Clean up resources (files, servers, handles)
- ✅ Use descriptive test names in console output
- ✅ Return 0 for success, >0 for failures
- ✅ Count and report number of failures
- ✅ Add comments explaining complex test logic

### DON'T:
- ❌ Mix unrelated API tests in one file
- ❌ Leave test files/directories on disk
- ❌ Use hardcoded ports (use port 0 for auto-assignment)
- ❌ Ignore errors or exceptions
- ❌ Write tests that depend on external services
- ❌ Write tests that require manual cleanup

## Error Handling

All tests should handle errors gracefully:

```typescript
try {
  // Test code
  if (actualValue !== expectedValue) {
    console.log('FAIL: Description of failure');
    failures++;
  } else {
    console.log('PASS: Description of success');
  }
} catch (e) {
  console.log('FAIL: Exception:', e.message || e);
  failures++;
}
```

## Async Patterns

### Pattern 1: Promises

```typescript
async function user_main(): Promise<number> {
  try {
    const result = await someAsyncOperation();
    console.log('PASS');
    return 0;
  } catch (e) {
    console.log('FAIL:', e);
    return 1;
  }
}
```

### Pattern 2: Callbacks with Promise Wrapper

```typescript
function user_main(): Promise<number> {
  return new Promise((resolve) => {
    fs.readFile('test.txt', (err, data) => {
      if (err) {
        console.log('FAIL:', err);
        resolve(1);
      } else {
        console.log('PASS');
        resolve(0);
      }
    });
  });
}
```

### Pattern 3: Event Emitters

```typescript
function user_main(): Promise<number> {
  return new Promise((resolve) => {
    const stream = fs.createReadStream('file.txt');
    let failures = 0;

    stream.on('data', (chunk) => {
      console.log('Received chunk');
    });

    stream.on('end', () => {
      console.log('PASS: Stream ended');
      resolve(failures);
    });

    stream.on('error', (err) => {
      console.log('FAIL:', err);
      resolve(1);
    });
  });
}
```

## Resource Cleanup

### Temporary Files

```typescript
const testFile = 'test_temp.txt';

try {
  fs.writeFileSync(testFile, 'content');
  // ... test logic ...
} finally {
  // Always clean up, even if test fails
  try {
    fs.unlinkSync(testFile);
  } catch (e) {
    // Ignore cleanup errors
  }
}
```

### Temporary Directories

```typescript
const testDir = fs.mkdtempSync('test-');

try {
  // ... test logic ...
} finally {
  // Recursive cleanup
  fs.rmSync(testDir, { recursive: true, force: true });
}
```

### Servers and Network Resources

```typescript
const server = http.createServer(...);

server.listen(0, () => {
  // ... test logic ...

  // Always close server
  server.close(() => {
    resolve(failures);
  });
});
```

## Compatibility Matrix Alignment

Each test file should map to features in the [compatibility matrix](../../docs/phase18/compatibility_matrix.md).

**Example mapping:**

| Test File | Compatibility Matrix Features |
|-----------|-------------------------------|
| `fs/fs_sync.ts` | fs.readFileSync, fs.writeFileSync, fs.unlinkSync, fs.statSync |
| `http/http_server.ts` | http.createServer, http.Server, http.IncomingMessage, http.ServerResponse |
| `buffer/buffer_basic.ts` | Buffer.alloc, Buffer.from, Buffer.concat, buffer.toString |

## Running Tests

### Compile and Run Single Test

```bash
# Compile
build/src/compiler/Release/ts-aot.exe tests/node/fs/fs_sync.ts -o tests/node/fs/fs_sync.exe

# Run
tests/node/fs/fs_sync.exe

# Check exit code
echo $?  # Should be 0 for success
```

### Run All Tests in Category

```bash
# Compile all fs tests
for file in tests/node/fs/*.ts; do
  build/src/compiler/Release/ts-aot.exe "$file" -o "${file%.ts}.exe"
done

# Run all
for file in tests/node/fs/*.exe; do
  echo "Running $file..."
  "$file"
  if [ $? -eq 0 ]; then
    echo "✓ PASS"
  else
    echo "✗ FAIL"
  fi
done
```

### Using Test Runner (Future)

```bash
# Run all Node.js API tests
python tests/node/runner.py

# Run specific category
python tests/node/runner.py fs

# Run with verbose output
python tests/node/runner.py --verbose fs
```

## Test Output Format

**Standard output format:**

```
PASS: fs.readFileSync reads file content
PASS: fs.writeFileSync creates file
FAIL: fs.unlinkSync on nonexistent file (Expected error not thrown)
```

**Exit codes:**
- `0` - All tests passed
- `1+` - Number of failed tests (or just 1 if not counting)

## Debugging Failed Tests

### Enable Debug Logging

```typescript
// Add verbose logging
console.log('Test: Reading file...');
const content = fs.readFileSync('test.txt', 'utf8');
console.log('Content:', content);
console.log('Length:', content.length);
```

### Use Compiler Debug Flags

```bash
# Dump types
ts-aot tests/node/fs/fs_sync.ts --dump-types -o test.exe

# Dump IR
ts-aot tests/node/fs/fs_sync.ts --dump-ir -o test.exe
```

### Use Auto-Debug Skill

If the test crashes:

```powershell
.\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath tests\node\fs\fs_sync.exe
```

## Contributing Tests

When adding a new Node.js API test:

1. **Check compatibility matrix** - Verify the API is implemented (🟢 or 🟡)
2. **Choose appropriate directory** - Match Node.js module organization
3. **Follow naming conventions** - `<module>_<feature>.ts`
4. **Include error cases** - Don't just test happy path
5. **Clean up resources** - Files, servers, handles
6. **Document special requirements** - Use `// SERVER-TEST: true` for servers
7. **Test locally** - Compile and run before committing
8. **Update this README** - If adding new patterns or categories

## Examples

See existing tests for patterns:

- **Basic sync I/O:** `fs/fs_sync.ts` (when created)
- **Async operations:** `fs/fs_async.ts` (when created)
- **Server pattern:** `http/http_server.ts` (when created)
- **Stream pattern:** `streams/readable_basic.ts` (when created)
- **Event emitter:** `events/events_basic.ts` (when created)

## Related Documentation

- [Compatibility Matrix](../../docs/phase18/compatibility_matrix.md) - API status
- [Epic 107: Test Cleanup](../../docs/phase19/epic_107_test_cleanup.md) - Test reorganization plan
- [Adding Node.js APIs](../../.github/instructions/adding-nodejs-api.instructions.md) - Implementation guide
- [Golden IR Tests](../golden_ir/README.md) - Runtime/language tests
