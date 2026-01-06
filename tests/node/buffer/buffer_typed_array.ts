// Buffer TypedArray-like Properties Tests
// Migrated from examples/buffer_typed_array_test.ts
// Tests Buffer's TypedArray-compatible properties

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer TypedArray-like Properties Tests ===\n');

  // Test 1: buffer.byteLength
  try {
    const buf1 = Buffer.alloc(10);
    if (buf1.byteLength !== 10) {
      console.log('FAIL: buffer.byteLength - wrong value');
      failures++;
    } else {
      console.log('PASS: buffer.byteLength equals length');
    }
  } catch (e) {
    console.log('FAIL: buffer.byteLength - Exception');
    failures++;
  }

  // Test 2: buffer.byteOffset is always 0 for Buffer
  try {
    const buf2 = Buffer.alloc(20);
    if (buf2.byteOffset !== 0) {
      console.log('FAIL: buffer.byteOffset - not 0');
      failures++;
    } else {
      console.log('PASS: buffer.byteOffset is 0');
    }
  } catch (e) {
    console.log('FAIL: buffer.byteOffset - Exception');
    failures++;
  }

  // Test 3: buffer.buffer returns ArrayBuffer - truthy check
  try {
    const buf3 = Buffer.alloc(5);
    buf3[0] = 42;
    const arrBuf = buf3.buffer;
    if (arrBuf === null) {
      console.log('FAIL: buffer.buffer - returned null');
      failures++;
    } else {
      console.log('PASS: buffer.buffer returns truthy value');
    }
  } catch (e) {
    console.log('FAIL: buffer.buffer truthy - Exception');
    failures++;
  }

  // Test 4: buffer.buffer preserves values
  try {
    const buf3 = Buffer.alloc(5);
    buf3[0] = 42;
    const arrBuf = buf3.buffer;
    if (arrBuf[0] !== 42) {
      console.log('FAIL: buffer.buffer - wrong value');
      failures++;
    } else {
      console.log('PASS: buffer.buffer preserves original values');
    }
  } catch (e) {
    console.log('FAIL: buffer.buffer values - Exception');
    failures++;
  }

  // Test 5: Empty buffer byteLength
  try {
    const buf4 = Buffer.alloc(0);
    if (buf4.byteLength !== 0) {
      console.log('FAIL: empty buffer.byteLength - not 0');
      failures++;
    } else {
      console.log('PASS: buffer.byteLength for empty buffer is 0');
    }
  } catch (e) {
    console.log('FAIL: empty buffer.byteLength - Exception');
    failures++;
  }

  // Test 6: Large buffer byteLength
  try {
    const buf5 = Buffer.alloc(1000);
    if (buf5.byteLength !== 1000) {
      console.log('FAIL: large buffer.byteLength - wrong value');
      failures++;
    } else {
      console.log('PASS: buffer.byteLength for large buffer');
    }
  } catch (e) {
    console.log('FAIL: large buffer.byteLength - Exception');
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
