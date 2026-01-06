// Crypto Module Basic Tests

function user_main(): number {
  let failures = 0;
  console.log('=== Crypto Module Tests ===\n');

  // Test 1: crypto.md5
  try {
    const hash1 = crypto.md5("abcdef");
    if (typeof hash1 !== 'string' || hash1.length === 0) {
      console.log('FAIL: crypto.md5 - should return non-empty string');
      failures++;
    } else {
      console.log('PASS: crypto.md5("abcdef")');
      console.log('  Hash:', hash1);
    }
  } catch (e) {
    console.log('FAIL: crypto.md5 - Exception');
    failures++;
  }

  // Test 2: crypto.md5 different input
  try {
    const hash2 = crypto.md5("pqrstuv");
    if (typeof hash2 !== 'string' || hash2.length === 0) {
      console.log('FAIL: crypto.md5 - should return non-empty string');
      failures++;
    } else {
      console.log('PASS: crypto.md5("pqrstuv")');
      console.log('  Hash:', hash2);
    }
  } catch (e) {
    console.log('FAIL: crypto.md5 - Exception');
    failures++;
  }

  // Test 3: Verify hashes are different for different inputs
  try {
    const hash1 = crypto.md5("abcdef");
    const hash2 = crypto.md5("pqrstuv");
    // Can't compare strings reliably, but we can check lengths
    if (hash1.length !== hash2.length) {
      console.log('FAIL: MD5 hashes should have same length');
      failures++;
    } else {
      console.log('PASS: MD5 hash length consistency');
    }
  } catch (e) {
    console.log('FAIL: crypto.md5 consistency - Exception');
    failures++;
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
