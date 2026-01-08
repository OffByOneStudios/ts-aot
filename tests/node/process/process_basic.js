// Process Module Basic Tests - JavaScript version

function user_main() {
  var failures = 0;

  console.log('=== process Module Tests (JS) ===\n');

  // Test 1: process.argv
  try {
    if (!process.argv || process.argv.length < 1) {
      console.log('FAIL: process.argv should have at least 1 element');
      failures = failures + 1;
    } else {
      console.log('PASS: process.argv (length: ' + process.argv.length + ')');
    }
  } catch (e) {
    console.log('FAIL: process.argv - Exception');
    failures = failures + 1;
  }

  // Test 2: process.cwd()
  try {
    var cwd = process.cwd();
    if (typeof cwd !== 'string' || cwd.length === 0) {
      console.log('FAIL: process.cwd() should return non-empty string');
      failures = failures + 1;
    } else {
      console.log('PASS: process.cwd()');
    }
  } catch (e) {
    console.log('FAIL: process.cwd() - Exception');
    failures = failures + 1;
  }

  // Test 3: process.platform
  try {
    var platform = process.platform;
    if (typeof platform !== 'string') {
      console.log('FAIL: process.platform should be a string');
      failures = failures + 1;
    } else {
      console.log('PASS: process.platform: ' + platform);
    }
  } catch (e) {
    console.log('FAIL: process.platform - Exception');
    failures = failures + 1;
  }

  // Test 4: process.arch
  try {
    var arch = process.arch;
    if (typeof arch !== 'string') {
      console.log('FAIL: process.arch should be a string');
      failures = failures + 1;
    } else {
      console.log('PASS: process.arch: ' + arch);
    }
  } catch (e) {
    console.log('FAIL: process.arch - Exception');
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
