// Asynchronous File System API Tests

import * as fs from 'fs';

async function runTests(): Promise<number> {
  let failures = 0;

  console.log('=== fs Asynchronous API Tests ===\n');

  // Test 1: promises.writeFile and promises.readFile
  try {
    const testFile = 'test_fs_async.txt';
    const content = 'Hello from ts-aot fs.promises.writeFile!';

    await fs.promises.writeFile(testFile, content);
    const readContent = await fs.promises.readFile(testFile, 'utf8');

    if (readContent !== content) {
      console.log('FAIL: writeFile/readFile - Content mismatch');
      console.log('  Expected:', content);
      console.log('  Got:', readContent);
      failures++;
    } else {
      console.log('PASS: fs.promises.writeFile and fs.promises.readFile');
    }

    // Cleanup
    await fs.promises.unlink(testFile);
  } catch (e) {
    console.log('FAIL: writeFile/readFile - Exception:', e.message || e);
    failures++;
  }

  // Test 2: promises.mkdir and promises.rmdir
  try {
    const testDir = 'test_dir_async';

    await fs.promises.mkdir(testDir);

    // Verify it exists
    if (!fs.existsSync(testDir)) {
      console.log('FAIL: mkdir - Directory should exist');
      failures++;
    }

    // Check stats
    const stats = await fs.promises.stat(testDir);
    if (!stats.isDirectory()) {
      console.log('FAIL: mkdir - Should be a directory');
      failures++;
    } else {
      console.log('PASS: fs.promises.mkdir');
    }

    await fs.promises.rmdir(testDir);

    // Verify it's gone
    if (fs.existsSync(testDir)) {
      console.log('FAIL: rmdir - Directory should be removed');
      failures++;
    } else {
      console.log('PASS: fs.promises.rmdir');
    }
  } catch (e) {
    console.log('FAIL: mkdir/rmdir - Exception:', e.message || e);
    failures++;
  }

  // Test 3: promises.stat
  try {
    const testFile = 'test_async_stat.txt';
    const content = 'test data';

    await fs.promises.writeFile(testFile, content);
    const stats = await fs.promises.stat(testFile);

    if (!stats.isFile()) {
      console.log('FAIL: stat - Should be a file');
      failures++;
    } else if (stats.size !== content.length) {
      console.log('FAIL: stat - Size mismatch');
      console.log('  Expected:', content.length);
      console.log('  Got:', stats.size);
      failures++;
    } else {
      console.log('PASS: fs.promises.stat');
    }

    // Cleanup
    await fs.promises.unlink(testFile);
  } catch (e) {
    console.log('FAIL: stat - Exception:', e.message || e);
    failures++;
  }

  // Test 4: promises.unlink
  try {
    const testFile = 'test_async_unlink.txt';

    await fs.promises.writeFile(testFile, 'temporary');

    if (!fs.existsSync(testFile)) {
      console.log('FAIL: unlink - File should exist before unlink');
      failures++;
    }

    await fs.promises.unlink(testFile);

    if (fs.existsSync(testFile)) {
      console.log('FAIL: unlink - File should be removed');
      failures++;
    } else {
      console.log('PASS: fs.promises.unlink');
    }
  } catch (e) {
    console.log('FAIL: unlink - Exception:', e.message || e);
    failures++;
  }

  // Test 5: promises.appendFile
  try {
    const testFile = 'test_async_append.txt';

    await fs.promises.writeFile(testFile, 'Line 1\n');
    await fs.promises.appendFile(testFile, 'Line 2\n');

    const content = await fs.promises.readFile(testFile, 'utf8');
    const expected = 'Line 1\nLine 2\n';

    if (content !== expected) {
      console.log('FAIL: appendFile - Content mismatch');
      console.log('  Expected:', expected);
      console.log('  Got:', content);
      failures++;
    } else {
      console.log('PASS: fs.promises.appendFile');
    }

    // Cleanup
    await fs.promises.unlink(testFile);
  } catch (e) {
    console.log('FAIL: appendFile - Exception:', e.message || e);
    failures++;
  }

  // Test 6: Error handling - read nonexistent file
  try {
    await fs.promises.readFile('nonexistent_async_file_12345.txt');
    console.log('FAIL: readFile should reject on nonexistent file');
    failures++;
  } catch (e) {
    console.log('PASS: fs.promises.readFile error handling');
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(`${failures} test(s) failed`);
  }

  return failures;
}

async function user_main(): Promise<number> {
  return await runTests();
}
