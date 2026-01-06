// Filesystem Directory API Tests (mkdir, rmdir, readdir, opendir)
import * as fs from 'fs';

function user_main(): number {
  let failures = 0;
  console.log('=== Filesystem Directory Tests ===\n');

  const testDir = 'tmp/test_dir_ops';
  const testSubDir = 'tmp/test_dir_ops/subdir';
  const testFile1 = 'tmp/test_dir_ops/file1.txt';
  const testFile2 = 'tmp/test_dir_ops/file2.txt';

  // Test 1: fs.mkdirSync creates directory
  try {
    if (fs.existsSync(testDir)) {
      fs.rmdirSync(testDir);
    }
    fs.mkdirSync(testDir);
    if (!fs.existsSync(testDir)) {
      console.log('FAIL: fs.mkdirSync did not create directory');
      failures++;
    } else {
      console.log('PASS: fs.mkdirSync creates directory');
    }
  } catch (e) {
    console.log('FAIL: fs.mkdirSync - Exception');
    failures++;
  }

  // Test 2: fs.mkdirSync creates subdirectory
  try {
    fs.mkdirSync(testSubDir);
    if (!fs.existsSync(testSubDir)) {
      console.log('FAIL: fs.mkdirSync did not create subdirectory');
      failures++;
    } else {
      console.log('PASS: fs.mkdirSync creates subdirectory');
    }
  } catch (e) {
    console.log('FAIL: fs.mkdirSync subdirectory - Exception');
    failures++;
  }

  // Setup: Create test files
  try {
    fs.writeFileSync(testFile1, 'file1 content');
    fs.writeFileSync(testFile2, 'file2 content');
  } catch (e) {
    console.log('FAIL: Setup - could not create test files');
    return 1;
  }

  // Test 3: fs.readdirSync returns array
  try {
    const entries = fs.readdirSync(testDir);
    if (typeof entries.length !== 'number') {
      console.log('FAIL: fs.readdirSync did not return array');
      failures++;
    } else {
      console.log('PASS: fs.readdirSync returns array');
    }
  } catch (e) {
    console.log('FAIL: fs.readdirSync - Exception');
    failures++;
  }

  // Test 4: fs.readdirSync returns correct count
  try {
    const entries = fs.readdirSync(testDir);
    if (entries.length !== 3) {
      console.log('FAIL: fs.readdirSync wrong count');
      failures++;
    } else {
      console.log('PASS: fs.readdirSync correct count');
    }
  } catch (e) {
    console.log('FAIL: fs.readdirSync count - Exception');
    failures++;
  }

  // Test 5: fs.opendirSync returns Dir object
  try {
    const dir = fs.opendirSync(testDir);
    if (typeof dir.path !== 'string') {
      console.log('FAIL: fs.opendirSync did not return Dir with path');
      failures++;
    } else {
      console.log('PASS: fs.opendirSync returns Dir');
    }
    dir.closeSync();
  } catch (e) {
    console.log('FAIL: fs.opendirSync - Exception');
    failures++;
  }

  // Test 6: Dir.readSync returns entries
  try {
    const dir = fs.opendirSync(testDir);
    const entry = dir.readSync();
    if (!entry || typeof entry.name !== 'string') {
      console.log('FAIL: Dir.readSync did not return entry');
      failures++;
    } else {
      console.log('PASS: Dir.readSync returns entry');
    }
    dir.closeSync();
  } catch (e) {
    console.log('FAIL: Dir.readSync - Exception');
    failures++;
  }

  // Test 7: Dir.readSync returns all entries
  try {
    const dir = fs.opendirSync(testDir);
    let count = 0;
    let entry = dir.readSync();
    while (entry) {
      count++;
      entry = dir.readSync();
    }
    if (count !== 3) {
      console.log('FAIL: Dir.readSync wrong entry count');
      failures++;
    } else {
      console.log('PASS: Dir.readSync iterates all entries');
    }
    dir.closeSync();
  } catch (e) {
    console.log('FAIL: Dir.readSync iteration - Exception');
    failures++;
  }

  // Test 8: Dirent.isFile()
  try {
    const dir = fs.opendirSync(testDir);
    let entry = dir.readSync();
    let foundFile = false;
    while (entry) {
      if (entry.isFile()) {
        foundFile = true;
        break;
      }
      entry = dir.readSync();
    }
    if (!foundFile) {
      console.log('FAIL: Dirent.isFile() did not find file');
      failures++;
    } else {
      console.log('PASS: Dirent.isFile()');
    }
    dir.closeSync();
  } catch (e) {
    console.log('FAIL: Dirent.isFile() - Exception');
    failures++;
  }

  // Test 9: Dirent.isDirectory()
  try {
    const dir = fs.opendirSync(testDir);
    let entry = dir.readSync();
    let foundDir = false;
    while (entry) {
      if (entry.isDirectory()) {
        foundDir = true;
        break;
      }
      entry = dir.readSync();
    }
    if (!foundDir) {
      console.log('FAIL: Dirent.isDirectory() did not find directory');
      failures++;
    } else {
      console.log('PASS: Dirent.isDirectory()');
    }
    dir.closeSync();
  } catch (e) {
    console.log('FAIL: Dirent.isDirectory() - Exception');
    failures++;
  }

  // Test 10: fs.rmdirSync removes empty directory
  try {
    fs.rmdirSync(testSubDir);
    if (fs.existsSync(testSubDir)) {
      console.log('FAIL: fs.rmdirSync did not remove directory');
      failures++;
    } else {
      console.log('PASS: fs.rmdirSync removes directory');
    }
  } catch (e) {
    console.log('FAIL: fs.rmdirSync - Exception');
    failures++;
  }

  // Cleanup
  try {
    fs.unlinkSync(testFile1);
    fs.unlinkSync(testFile2);
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
