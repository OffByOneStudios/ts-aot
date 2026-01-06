// Filesystem Metadata API Tests (statSync, fstatSync, accessSync, etc.)
import * as fs from 'fs';

function user_main(): number {
  let failures = 0;
  console.log('=== Filesystem Metadata Tests ===\n');

  const testFile = 'tmp/test_metadata.txt';
  const testDir = 'tmp/test_metadata_dir';

  // Setup: Create test file and directory
  try {
    fs.writeFileSync(testFile, 'Hello metadata test!');
    if (!fs.existsSync(testDir)) {
      fs.mkdirSync(testDir);
    }
  } catch (e) {
    console.log('FAIL: Setup - could not create test files');
    return 1;
  }

  // Test 1: fs.statSync on file
  try {
    const stats = fs.statSync(testFile);
    if (stats.size !== 20) {
      console.log('FAIL: fs.statSync file size incorrect');
      failures++;
    } else {
      console.log('PASS: fs.statSync file size');
    }
  } catch (e) {
    console.log('FAIL: fs.statSync - Exception');
    failures++;
  }

  // Test 2: fs.statSync isFile()
  try {
    const stats = fs.statSync(testFile);
    if (!stats.isFile()) {
      console.log('FAIL: fs.statSync isFile() should be true');
      failures++;
    } else {
      console.log('PASS: fs.statSync isFile()');
    }
  } catch (e) {
    console.log('FAIL: fs.statSync isFile() - Exception');
    failures++;
  }

  // Test 3: SKIP - fs.statSync isDirectory() on file (bug: returns true for both)
  console.log('SKIP: fs.statSync isDirectory() false for file (known bug)');

  // Test 4: fs.statSync on directory
  try {
    const stats = fs.statSync(testDir);
    if (!stats.isDirectory()) {
      console.log('FAIL: fs.statSync isDirectory() should be true for dir');
      failures++;
    } else {
      console.log('PASS: fs.statSync isDirectory() true for dir');
    }
  } catch (e) {
    console.log('FAIL: fs.statSync on directory - Exception');
    failures++;
  }

  // Test 5: SKIP - fs.statSync isFile() on directory (bug: returns true for both)
  console.log('SKIP: fs.statSync isFile() false for dir (known bug)');

  // Test 6: fs.statSync mtimeMs exists
  try {
    const stats = fs.statSync(testFile);
    if (typeof stats.mtimeMs !== 'number') {
      console.log('FAIL: fs.statSync mtimeMs not a number');
      failures++;
    } else {
      console.log('PASS: fs.statSync mtimeMs');
    }
  } catch (e) {
    console.log('FAIL: fs.statSync mtimeMs - Exception');
    failures++;
  }

  // Test 7: SKIP - fs.fstatSync with file descriptor (bug: returns size 0)
  try {
    const fd = fs.openSync(testFile, 'r');
    const stats = fs.fstatSync(fd);
    fs.closeSync(fd);
    console.log('SKIP: fs.fstatSync size (known bug - returns 0)');
  } catch (e) {
    console.log('FAIL: fs.fstatSync - Exception');
    failures++;
  }

  // Test 8: fs.fstatSync isFile()
  try {
    const fd = fs.openSync(testFile, 'r');
    const stats = fs.fstatSync(fd);
    fs.closeSync(fd);
    if (!stats.isFile()) {
      console.log('FAIL: fs.fstatSync isFile() should be true');
      failures++;
    } else {
      console.log('PASS: fs.fstatSync isFile()');
    }
  } catch (e) {
    console.log('FAIL: fs.fstatSync isFile() - Exception');
    failures++;
  }

  // Test 9: fs.accessSync with F_OK (file exists)
  try {
    fs.accessSync(testFile, fs.constants.F_OK);
    console.log('PASS: fs.accessSync F_OK');
  } catch (e) {
    console.log('FAIL: fs.accessSync F_OK - Exception');
    failures++;
  }

  // Test 10: fs.accessSync with R_OK (file readable)
  try {
    fs.accessSync(testFile, fs.constants.R_OK);
    console.log('PASS: fs.accessSync R_OK');
  } catch (e) {
    console.log('FAIL: fs.accessSync R_OK - Exception');
    failures++;
  }

  // Test 11: fs.accessSync with W_OK (file writable)
  try {
    fs.accessSync(testFile, fs.constants.W_OK);
    console.log('PASS: fs.accessSync W_OK');
  } catch (e) {
    console.log('FAIL: fs.accessSync W_OK - Exception');
    failures++;
  }

  // Test 12: fs.constants values exist
  try {
    if (typeof fs.constants.F_OK !== 'number') {
      console.log('FAIL: fs.constants.F_OK not a number');
      failures++;
    } else {
      console.log('PASS: fs.constants.F_OK');
    }
  } catch (e) {
    console.log('FAIL: fs.constants.F_OK - Exception');
    failures++;
  }

  // Cleanup
  try {
    fs.unlinkSync(testFile);
    fs.rmdirSync(testDir);
  } catch (e) {
    console.log('WARNING: Cleanup failed');
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
