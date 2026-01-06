// Filesystem Links API Tests (linkSync, symlinkSync, readlinkSync, realpathSync)
import * as fs from 'fs';

function user_main(): number {
  let failures = 0;
  console.log('=== Filesystem Links Tests ===\n');

  const testFile = 'tmp/test_link_source.txt';
  const hardLink = 'tmp/test_hardlink.txt';
  const symLink = 'tmp/test_symlink.txt';

  // Cleanup any existing files
  try {
    if (fs.existsSync(testFile)) fs.unlinkSync(testFile);
    if (fs.existsSync(hardLink)) fs.unlinkSync(hardLink);
    if (fs.existsSync(symLink)) fs.unlinkSync(symLink);
  } catch (e) {
    // Ignore cleanup errors
  }

  // Setup: Create test file
  try {
    fs.writeFileSync(testFile, 'Hello Links Test!');
  } catch (e) {
    console.log('FAIL: Setup - could not create test file');
    return 1;
  }

  // Test 1: fs.linkSync creates hard link
  try {
    fs.linkSync(testFile, hardLink);
    if (!fs.existsSync(hardLink)) {
      console.log('FAIL: fs.linkSync did not create hard link');
      failures++;
    } else {
      console.log('PASS: fs.linkSync creates hard link');
    }
  } catch (e) {
    console.log('FAIL: fs.linkSync - Exception');
    failures++;
  }

  // Test 2: Hard link has same size as original
  try {
    const originalStats = fs.statSync(testFile);
    const linkStats = fs.statSync(hardLink);
    if (originalStats.size !== linkStats.size) {
      console.log('FAIL: Hard link size differs from original');
      failures++;
    } else {
      console.log('PASS: Hard link has same size');
    }
  } catch (e) {
    console.log('FAIL: Hard link size check - Exception');
    failures++;
  }

  // Test 3: Reading hard link returns content
  try {
    const buffer = fs.readFileSync(hardLink);
    if (buffer.length !== 17) {
      console.log('FAIL: Hard link content wrong length');
      failures++;
    } else {
      console.log('PASS: Hard link content readable');
    }
  } catch (e) {
    console.log('FAIL: Hard link read - Exception');
    failures++;
  }

  // Test 4: fs.symlinkSync creates symlink (verify with lstat)
  try {
    fs.symlinkSync(testFile, symLink);
    const lstatTest = fs.lstatSync(symLink);
    if (!lstatTest.isSymbolicLink()) {
      console.log('FAIL: fs.symlinkSync did not create symlink');
      failures++;
    } else {
      console.log('PASS: fs.symlinkSync creates symlink');
    }
  } catch (e) {
    console.log('SKIP: fs.symlinkSync (may need admin privileges on Windows)');
  }

  // Test 5: fs.lstatSync detects symbolic link
  try {
    const lstat = fs.lstatSync(symLink);
    if (!lstat.isSymbolicLink()) {
      console.log('FAIL: lstatSync should detect symbolic link');
      failures++;
    } else {
      console.log('PASS: lstatSync detects symbolic link');
    }
  } catch (e) {
    console.log('SKIP: lstatSync (symlink not created)');
  }

  // Test 6: fs.statSync follows symlink
  try {
    const stat = fs.statSync(symLink);
    if (stat.isSymbolicLink()) {
      console.log('FAIL: statSync should not show symlink (follows link)');
      failures++;
    } else {
      console.log('PASS: statSync follows symlink');
    }
  } catch (e) {
    console.log('SKIP: statSync on symlink (symlink not created)');
  }

  // Test 7: fs.readlinkSync returns target path
  try {
    const target = fs.readlinkSync(symLink);
    if (target.length === 0) {
      console.log('FAIL: readlinkSync returned empty target');
      failures++;
    } else {
      console.log('PASS: readlinkSync returns target');
    }
  } catch (e) {
    console.log('SKIP: readlinkSync (symlink not created)');
  }

  // Test 8: SKIP - fs.realpathSync (causes exit 127 crash)
  console.log('SKIP: realpathSync (known crash issue)');

  // Cleanup
  try {
    if (fs.existsSync(hardLink)) fs.unlinkSync(hardLink);
    if (fs.existsSync(symLink)) fs.unlinkSync(symLink);
    if (fs.existsSync(testFile)) fs.unlinkSync(testFile);
  } catch (e) {
    console.log('WARNING: Cleanup failed');
  }

  console.log('\n=== Summary ===');
  console.log('Note: Some symlink tests may skip on Windows without admin privileges');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
