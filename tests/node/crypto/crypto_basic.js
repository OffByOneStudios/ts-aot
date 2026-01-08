// Crypto Module Basic Tests - JavaScript version

function user_main() {
  var failures = 0;
  console.log('=== Crypto Module Tests (JS) ===\n');

  // Test 1: crypto.md5
  try {
    var hash1 = crypto.md5("abcdef");
    if (typeof hash1 !== 'string' || hash1.length === 0) {
      console.log('FAIL: crypto.md5 - should return non-empty string');
      failures = failures + 1;
    } else {
      console.log('PASS: crypto.md5("abcdef")');
    }
  } catch (e) {
    console.log('FAIL: crypto.md5 - Exception');
    failures = failures + 1;
  }

  // Test 2: crypto.md5 different input
  try {
    var hash2 = crypto.md5("pqrstuv");
    if (typeof hash2 !== 'string' || hash2.length === 0) {
      console.log('FAIL: crypto.md5 - should return non-empty string');
      failures = failures + 1;
    } else {
      console.log('PASS: crypto.md5("pqrstuv")');
    }
  } catch (e) {
    console.log('FAIL: crypto.md5 - Exception');
    failures = failures + 1;
  }

  // Test 3: Verify hash length consistency
  try {
    var hash1b = crypto.md5("abcdef");
    var hash2b = crypto.md5("pqrstuv");
    if (hash1b.length !== hash2b.length) {
      console.log('FAIL: MD5 hashes should have same length');
      failures = failures + 1;
    } else {
      console.log('PASS: MD5 hash length consistency');
    }
  } catch (e) {
    console.log('FAIL: crypto.md5 consistency - Exception');
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
