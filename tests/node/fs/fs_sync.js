// Synchronous File System API Tests - JavaScript version

var fs = require('fs');

function user_main() {
  var failures = 0;

  console.log('=== fs Synchronous API Tests (JS) ===\n');

  // Test 1: writeFileSync and readFileSync
  try {
    var testFile = 'test_fs_sync_js.txt';
    var content = 'Hello from ts-aot fs.writeFileSync JS!';

    fs.writeFileSync(testFile, content);
    var readContent = fs.readFileSync(testFile, 'utf8').toString();

    if (readContent !== content) {
      console.log('FAIL: writeFileSync/readFileSync - Content mismatch');
      failures = failures + 1;
    } else {
      console.log('PASS: fs.writeFileSync and fs.readFileSync');
    }

    // Cleanup
    fs.unlinkSync(testFile);
  } catch (e) {
    console.log('FAIL: writeFileSync/readFileSync - Exception');
    failures = failures + 1;
  }

  // Test 2: existsSync
  try {
    var testFile2 = 'test_exists_js.txt';

    // Should not exist yet
    if (fs.existsSync(testFile2)) {
      console.log('FAIL: existsSync - File should not exist yet');
      failures = failures + 1;
    }

    // Create file
    fs.writeFileSync(testFile2, 'test');

    // Should exist now
    if (!fs.existsSync(testFile2)) {
      console.log('FAIL: existsSync - File should exist');
      failures = failures + 1;
    } else {
      console.log('PASS: fs.existsSync');
    }

    // Cleanup
    fs.unlinkSync(testFile2);
  } catch (e) {
    console.log('FAIL: existsSync - Exception');
    failures = failures + 1;
  }

  // Test 3: statSync
  try {
    var testFile3 = 'test_stat_js.txt';
    var content3 = '12345';

    fs.writeFileSync(testFile3, content3);
    var stats = fs.statSync(testFile3);

    if (!stats.isFile()) {
      console.log('FAIL: statSync - Should be a file');
      failures = failures + 1;
    } else if (stats.size !== content3.length) {
      console.log('FAIL: statSync - Size mismatch');
      failures = failures + 1;
    } else {
      console.log('PASS: fs.statSync');
    }

    // Cleanup
    fs.unlinkSync(testFile3);
  } catch (e) {
    console.log('FAIL: statSync - Exception');
    failures = failures + 1;
  }

  // Test 4: mkdirSync and rmdirSync
  try {
    var testDir = 'test_dir_sync_js';

    // Create directory
    fs.mkdirSync(testDir);

    // Should exist now
    if (!fs.existsSync(testDir)) {
      console.log('FAIL: mkdirSync - Directory should exist');
      failures = failures + 1;
    } else {
      console.log('PASS: fs.mkdirSync');
    }

    // Remove directory
    fs.rmdirSync(testDir);

    if (fs.existsSync(testDir)) {
      console.log('FAIL: rmdirSync - Directory should be removed');
      failures = failures + 1;
    } else {
      console.log('PASS: fs.rmdirSync');
    }
  } catch (e) {
    console.log('FAIL: mkdirSync/rmdirSync - Exception');
    failures = failures + 1;
  }

  // Test 5: unlinkSync
  try {
    var testFile5 = 'test_unlink_js.txt';

    fs.writeFileSync(testFile5, 'temporary');

    if (!fs.existsSync(testFile5)) {
      console.log('FAIL: unlinkSync - File should exist before unlink');
      failures = failures + 1;
    }

    fs.unlinkSync(testFile5);

    if (fs.existsSync(testFile5)) {
      console.log('FAIL: unlinkSync - File should be removed');
      failures = failures + 1;
    } else {
      console.log('PASS: fs.unlinkSync');
    }
  } catch (e) {
    console.log('FAIL: unlinkSync - Exception');
    failures = failures + 1;
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
