// Buffer Module Basic Tests - JavaScript version

function user_main() {
  var failures = 0;
  console.log('=== Buffer Module Tests (JS) ===\n');

  // Test 1: Buffer.alloc
  try {
    var buf = Buffer.alloc(10);
    if (buf.length !== 10) {
      console.log('FAIL: Buffer.alloc - wrong length');
      failures = failures + 1;
    } else {
      console.log('PASS: Buffer.alloc');
    }
  } catch (e) {
    console.log('FAIL: Buffer.alloc - Exception');
    failures = failures + 1;
  }

  // Test 2: Buffer.from string
  try {
    var buf2 = Buffer.from("Hello");
    if (buf2.length !== 5) {
      console.log('FAIL: Buffer.from - wrong length');
      failures = failures + 1;
    } else {
      console.log('PASS: Buffer.from string');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from - Exception');
    failures = failures + 1;
  }

  // Test 3: Buffer.toString
  try {
    var buf3 = Buffer.from("World");
    var str = buf3.toString();
    if (typeof str !== 'string') {
      console.log('FAIL: Buffer.toString - not a string');
      failures = failures + 1;
    } else {
      console.log('PASS: Buffer.toString');
    }
  } catch (e) {
    console.log('FAIL: Buffer.toString - Exception');
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
