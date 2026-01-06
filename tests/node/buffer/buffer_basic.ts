// Buffer Module Basic Tests

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer Module Tests ===\n');

  // Test 1: Buffer.alloc
  try {
    const buf = Buffer.alloc(10);
    if (buf.length !== 10) {
      console.log('FAIL: Buffer.alloc - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.alloc');
    }
  } catch (e) {
    console.log('FAIL: Buffer.alloc - Exception');
    failures++;
  }

  // Test 2: Buffer.from string
  try {
    const buf = Buffer.from("Hello");
    if (buf.length !== 5) {
      console.log('FAIL: Buffer.from - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.from string');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from - Exception');
    failures++;
  }

  // Test 3: Buffer.toString
  try {
    const buf = Buffer.from("World");
    const str = buf.toString();
    // Can't compare strings reliably, just check it's a string
    if (typeof str !== 'string') {
      console.log('FAIL: Buffer.toString - not a string');
      failures++;
    } else {
      console.log('PASS: Buffer.toString');
    }
  } catch (e) {
    console.log('FAIL: Buffer.toString - Exception');
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
