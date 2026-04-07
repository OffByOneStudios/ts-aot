// Template for Node.js API tests
// Copy this file and modify for your specific test needs

// ============================================================================
// BASIC SYNCHRONOUS TEST TEMPLATE
// ============================================================================

/*
import * as <module> from '<module>';

function user_main(): number {
  let failures = 0;

  // Test 1: Basic functionality
  try {
    // Setup
    // ... prepare test data ...

    // Execute
    const result = <module>.someFunction(...);

    // Verify
    if (result !== expectedValue) {
      console.log('FAIL: Test 1 - Description of what failed');
      console.log('  Expected:', expectedValue);
      console.log('  Got:', result);
      failures++;
    } else {
      console.log('PASS: Test 1 - Description of success');
    }

    // Cleanup
    // ... clean up resources ...
  } catch (e) {
    console.log('FAIL: Test 1 - Exception thrown');
    console.log('  Error:', e.message || e);
    failures++;
  }

  // Test 2: Error handling
  try {
    // Try something that should fail
    <module>.someFunctionThatShouldThrow(...);

    // If we get here, test failed
    console.log('FAIL: Test 2 - Should have thrown error');
    failures++;
  } catch (e) {
    // Expected error
    console.log('PASS: Test 2 - Correct error handling');
  }

  // Return number of failures (0 = all passed)
  return failures;
}
*/

// ============================================================================
// ASYNC/PROMISE TEST TEMPLATE
// ============================================================================

/*
import * as <module> from '<module>';

async function runTests(): Promise<number> {
  let failures = 0;

  // Test 1: Async operation
  try {
    const result = await <module>.asyncFunction(...);

    if (result !== expectedValue) {
      console.log('FAIL: Async test - Description');
      failures++;
    } else {
      console.log('PASS: Async test - Description');
    }
  } catch (e) {
    console.log('FAIL: Async test - Exception:', e.message || e);
    failures++;
  }

  return failures;
}

async function user_main(): Promise<number> {
  return await runTests();
}
*/

// ============================================================================
// SERVER TEST TEMPLATE
// ============================================================================

/*
// SERVER-TEST: true
// TIMEOUT: 5000

import * as http from 'http';

function user_main(): Promise<number> {
  return new Promise<number>((resolve) => {
    let failures = 0;

    // Create server
    const server = http.createServer((req, res) => {
      // Handle request
      res.writeHead(200, { 'Content-Type': 'text/plain' });
      res.end('Response data');
    });

    // Listen on random port
    server.listen(0, '127.0.0.1', () => {
      const addr = server.address();
      const port = addr.port;

      console.log(`Server listening on port ${port}`);

      // Make client request to own server
      http.get(`http://localhost:${port}/test`, (res) => {
        let data = '';

        res.on('data', (chunk) => {
          data += chunk;
        });

        res.on('end', () => {
          // Verify response
          if (data !== 'Response data') {
            console.log('FAIL: Response mismatch');
            failures++;
          } else {
            console.log('PASS: Server test');
          }

          // Shutdown server
          server.close(() => {
            resolve(failures);
          });
        });
      }).on('error', (err) => {
        console.log('FAIL: Request error:', err.message);
        failures++;
        server.close(() => resolve(failures));
      });
    });

    server.on('error', (err) => {
      console.log('FAIL: Server error:', err.message);
      resolve(1);
    });
  });
}
*/

// ============================================================================
// STREAM TEST TEMPLATE
// ============================================================================

/*
import * as fs from 'fs';
import * as stream from 'stream';

function user_main(): Promise<number> {
  return new Promise<number>((resolve) => {
    let failures = 0;

    // Setup test file
    fs.writeFileSync('test.txt', 'Stream test data');

    // Create readable stream
    const readable = fs.createReadStream('test.txt');
    let data = '';

    readable.on('data', (chunk) => {
      data += chunk;
    });

    readable.on('end', () => {
      // Verify data
      if (data !== 'Stream test data') {
        console.log('FAIL: Stream data mismatch');
        failures++;
      } else {
        console.log('PASS: Stream read');
      }

      // Cleanup
      try {
        fs.unlinkSync('test.txt');
      } catch (e) {
        // Ignore cleanup errors
      }

      resolve(failures);
    });

    readable.on('error', (err) => {
      console.log('FAIL: Stream error:', err.message);
      failures++;

      try {
        fs.unlinkSync('test.txt');
      } catch (e) {}

      resolve(failures);
    });
  });
}
*/

// ============================================================================
// EVENT EMITTER TEST TEMPLATE
// ============================================================================

/*
import { EventEmitter } from 'events';

function user_main(): Promise<number> {
  return new Promise<number>((resolve) => {
    let failures = 0;
    const emitter = new EventEmitter();

    // Setup listener
    emitter.on('test', (arg1, arg2) => {
      if (arg1 !== 'expected1' || arg2 !== 'expected2') {
        console.log('FAIL: Event args mismatch');
        failures++;
      } else {
        console.log('PASS: Event emitted with correct args');
      }

      resolve(failures);
    });

    // Emit event
    emitter.emit('test', 'expected1', 'expected2');
  });
}
*/

// ============================================================================
// COMPREHENSIVE TEST TEMPLATE (Multiple Tests)
// ============================================================================

/*
import * as <module> from '<module>';

interface TestCase {
  name: string;
  fn: () => boolean | Promise<boolean>;
}

async function runTest(test: TestCase): Promise<boolean> {
  try {
    const result = await test.fn();
    if (result) {
      console.log(`PASS: ${test.name}`);
      return true;
    } else {
      console.log(`FAIL: ${test.name}`);
      return false;
    }
  } catch (e) {
    console.log(`FAIL: ${test.name} - Exception: ${e.message || e}`);
    return false;
  }
}

async function user_main(): Promise<number> {
  const tests: TestCase[] = [
    {
      name: 'Test 1: Basic functionality',
      fn: () => {
        const result = <module>.function1();
        return result === expectedValue;
      }
    },
    {
      name: 'Test 2: Error handling',
      fn: () => {
        try {
          <module>.functionThatShouldThrow();
          return false; // Should have thrown
        } catch (e) {
          return true; // Expected error
        }
      }
    },
    {
      name: 'Test 3: Async operation',
      fn: async () => {
        const result = await <module>.asyncFunction();
        return result === expectedValue;
      }
    }
  ];

  let passed = 0;
  let failed = 0;

  for (const test of tests) {
    const result = await runTest(test);
    if (result) {
      passed++;
    } else {
      failed++;
    }
  }

  console.log(`\nResults: ${passed} passed, ${failed} failed`);
  return failed;
}
*/

// ============================================================================
// RESOURCE CLEANUP TEMPLATE
// ============================================================================

/*
import * as fs from 'fs';

function user_main(): number {
  let failures = 0;

  // Create temporary resources
  const testFile = 'test_temp.txt';
  const testDir = fs.mkdtempSync('test-');

  try {
    // Your test logic here
    fs.writeFileSync(testFile, 'test content');

    // ... tests ...

  } catch (e) {
    console.log('FAIL: Exception:', e.message || e);
    failures++;
  } finally {
    // ALWAYS clean up, even if tests fail

    // Clean up file
    try {
      if (fs.existsSync(testFile)) {
        fs.unlinkSync(testFile);
      }
    } catch (e) {
      // Ignore cleanup errors
    }

    // Clean up directory
    try {
      if (fs.existsSync(testDir)) {
        fs.rmSync(testDir, { recursive: true, force: true });
      }
    } catch (e) {
      // Ignore cleanup errors
    }
  }

  return failures;
}
*/

// ============================================================================
// ACTUAL TEMPLATE (Remove comments above and use this)
// ============================================================================

import * as assert from 'assert'; // If using assertions

function user_main(): number {
  let failures = 0;

  // TODO: Add your tests here

  console.log('PASS: Template test');

  return failures;
}
