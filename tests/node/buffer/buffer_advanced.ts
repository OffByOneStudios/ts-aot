// Buffer Advanced API Tests
// Migrated from examples/buffer_api_test.ts
// Tests comprehensive Buffer functionality including fill, slice, copy, concat

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer Advanced API Tests ===\n');

  // Test 1: Buffer.alloc
  try {
    const buf1 = Buffer.alloc(10);
    if (buf1.length !== 10) {
      console.log('FAIL: Buffer.alloc - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.alloc creates buffer with correct length');
    }
  } catch (e) {
    console.log('FAIL: Buffer.alloc - Exception');
    failures++;
  }

  // Test 2: Buffer.alloc is zero-filled
  try {
    const buf1 = Buffer.alloc(10);
    const allZeros = buf1[0] === 0 && buf1[5] === 0 && buf1[9] === 0;
    if (!allZeros) {
      console.log('FAIL: Buffer.alloc not zero-filled');
      failures++;
    } else {
      console.log('PASS: Buffer.alloc is zero-filled');
    }
  } catch (e) {
    console.log('FAIL: Buffer.alloc zero-filled - Exception');
    failures++;
  }

  // Test 3: Buffer.allocUnsafe
  try {
    const buf2 = Buffer.allocUnsafe(5);
    if (buf2.length !== 5) {
      console.log('FAIL: Buffer.allocUnsafe - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.allocUnsafe creates buffer with correct length');
    }
  } catch (e) {
    console.log('FAIL: Buffer.allocUnsafe - Exception');
    failures++;
  }

  // Test 4: Buffer.from string length
  try {
    const buf3 = Buffer.from('hello');
    if (buf3.length !== 5) {
      console.log('FAIL: Buffer.from string - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.from string creates buffer with correct length');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from string length - Exception');
    failures++;
  }

  // Test 5: Buffer.from string has correct first byte
  try {
    const buf3 = Buffer.from('hello');
    if (buf3[0] !== 104) {
      console.log('FAIL: Buffer.from string - wrong first byte');
      failures++;
    } else {
      console.log('PASS: Buffer.from string has correct first byte');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from string first byte - Exception');
    failures++;
  }

  // Test 6: Buffer indexing set/get
  try {
    const buf1 = Buffer.alloc(10);
    buf1[0] = 42;
    if (buf1[0] !== 42) {
      console.log('FAIL: Buffer set/get incorrect');
      failures++;
    } else {
      console.log('PASS: Buffer set/get works correctly');
    }
  } catch (e) {
    console.log('FAIL: Buffer set/get - Exception');
    failures++;
  }

  // Test 7: Buffer can store 255
  try {
    const buf1 = Buffer.alloc(10);
    buf1[1] = 255;
    if (buf1[1] !== 255) {
      console.log('FAIL: Buffer cannot store 255');
      failures++;
    } else {
      console.log('PASS: Buffer can store 255');
    }
  } catch (e) {
    console.log('FAIL: Buffer store 255 - Exception');
    failures++;
  }

  // Test 8: buffer.fill - simple case
  try {
    const buf4 = Buffer.alloc(5);
    buf4.fill(7);
    const allSevens = buf4[0] === 7 && buf4[1] === 7 && buf4[2] === 7 && buf4[3] === 7 && buf4[4] === 7;
    if (!allSevens) {
      console.log('FAIL: buffer.fill does not fill entire buffer');
      failures++;
    } else {
      console.log('PASS: buffer.fill fills entire buffer');
    }
  } catch (e) {
    console.log('FAIL: buffer.fill - Exception');
    failures++;
  }

  // Test 9: buffer.fill with range - before range
  try {
    const buf5 = Buffer.alloc(10);
    buf5.fill(9, 2, 5);
    if (buf5[1] !== 0) {
      console.log('FAIL: buffer.fill with range - before range not 0');
      failures++;
    } else {
      console.log('PASS: buffer.fill with range - before range is 0');
    }
  } catch (e) {
    console.log('FAIL: buffer.fill with range (before) - Exception');
    failures++;
  }

  // Test 10: buffer.fill with range - start of range
  try {
    const buf5 = Buffer.alloc(10);
    buf5.fill(9, 2, 5);
    if (buf5[2] !== 9) {
      console.log('FAIL: buffer.fill with range - start not 9');
      failures++;
    } else {
      console.log('PASS: buffer.fill with range - start of range is 9');
    }
  } catch (e) {
    console.log('FAIL: buffer.fill with range (start) - Exception');
    failures++;
  }

  // Test 11: buffer.fill with range - end of range
  try {
    const buf5 = Buffer.alloc(10);
    buf5.fill(9, 2, 5);
    if (buf5[4] !== 9) {
      console.log('FAIL: buffer.fill with range - end not 9');
      failures++;
    } else {
      console.log('PASS: buffer.fill with range - end of range is 9');
    }
  } catch (e) {
    console.log('FAIL: buffer.fill with range (end) - Exception');
    failures++;
  }

  // Test 12: buffer.fill with range - after range
  try {
    const buf5 = Buffer.alloc(10);
    buf5.fill(9, 2, 5);
    if (buf5[5] !== 0) {
      console.log('FAIL: buffer.fill with range - after range not 0');
      failures++;
    } else {
      console.log('PASS: buffer.fill with range - after range is 0');
    }
  } catch (e) {
    console.log('FAIL: buffer.fill with range (after) - Exception');
    failures++;
  }

  // Test 13: buffer.slice length
  try {
    const buf6 = Buffer.alloc(10);
    buf6[0] = 0; buf6[1] = 1; buf6[2] = 2; buf6[3] = 3; buf6[4] = 4;
    buf6[5] = 5; buf6[6] = 6; buf6[7] = 7; buf6[8] = 8; buf6[9] = 9;
    const sliced = buf6.slice(2, 5);
    if (sliced.length !== 3) {
      console.log('FAIL: buffer.slice - wrong length');
      failures++;
    } else {
      console.log('PASS: buffer.slice length is correct');
    }
  } catch (e) {
    console.log('FAIL: buffer.slice length - Exception');
    failures++;
  }

  // Test 14: buffer.slice values
  try {
    const buf6 = Buffer.alloc(10);
    buf6[0] = 0; buf6[1] = 1; buf6[2] = 2; buf6[3] = 3; buf6[4] = 4;
    buf6[5] = 5; buf6[6] = 6; buf6[7] = 7; buf6[8] = 8; buf6[9] = 9;
    const sliced = buf6.slice(2, 5);
    if (!(sliced[0] === 2 && sliced[1] === 3 && sliced[2] === 4)) {
      console.log('FAIL: buffer.slice - wrong values');
      failures++;
    } else {
      console.log('PASS: buffer.slice copies correct values');
    }
  } catch (e) {
    console.log('FAIL: buffer.slice values - Exception');
    failures++;
  }

  // Test 15: buffer.subarray length
  try {
    const buf6 = Buffer.alloc(10);
    buf6[0] = 0; buf6[1] = 1; buf6[2] = 2; buf6[3] = 3; buf6[4] = 4;
    buf6[5] = 5; buf6[6] = 6; buf6[7] = 7; buf6[8] = 8; buf6[9] = 9;
    const subbed = buf6.subarray(3, 7);
    if (subbed.length !== 4) {
      console.log('FAIL: buffer.subarray - wrong length');
      failures++;
    } else {
      console.log('PASS: buffer.subarray length is correct');
    }
  } catch (e) {
    console.log('FAIL: buffer.subarray length - Exception');
    failures++;
  }

  // Test 16: buffer.subarray values
  try {
    const buf6 = Buffer.alloc(10);
    buf6[0] = 0; buf6[1] = 1; buf6[2] = 2; buf6[3] = 3; buf6[4] = 4;
    buf6[5] = 5; buf6[6] = 6; buf6[7] = 7; buf6[8] = 8; buf6[9] = 9;
    const subbed = buf6.subarray(3, 7);
    if (!(subbed[0] === 3 && subbed[3] === 6)) {
      console.log('FAIL: buffer.subarray - wrong values');
      failures++;
    } else {
      console.log('PASS: buffer.subarray copies correct values');
    }
  } catch (e) {
    console.log('FAIL: buffer.subarray values - Exception');
    failures++;
  }

  // Test 17: buffer.copy returns bytes copied
  try {
    const src = Buffer.alloc(5);
    src[0] = 1; src[1] = 2; src[2] = 3; src[3] = 4; src[4] = 5;
    const dst = Buffer.alloc(10);
    const copied = src.copy(dst, 2);
    if (copied !== 5) {
      console.log('FAIL: buffer.copy - wrong return value');
      failures++;
    } else {
      console.log('PASS: buffer.copy returns bytes copied');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy returns - Exception');
    failures++;
  }

  // Test 18: buffer.copy to correct position
  try {
    const src = Buffer.alloc(5);
    src[0] = 1; src[1] = 2; src[2] = 3; src[3] = 4; src[4] = 5;
    const dst = Buffer.alloc(10);
    src.copy(dst, 2);
    if (!(dst[2] === 1 && dst[3] === 2 && dst[6] === 5)) {
      console.log('FAIL: buffer.copy - wrong position');
      failures++;
    } else {
      console.log('PASS: buffer.copy to correct position');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy position - Exception');
    failures++;
  }

  // Test 19: buffer.copy leaves other bytes unchanged
  try {
    const src = Buffer.alloc(5);
    src[0] = 1; src[1] = 2; src[2] = 3; src[3] = 4; src[4] = 5;
    const dst = Buffer.alloc(10);
    src.copy(dst, 2);
    if (!(dst[0] === 0 && dst[1] === 0)) {
      console.log('FAIL: buffer.copy - modified other bytes');
      failures++;
    } else {
      console.log('PASS: buffer.copy leaves other bytes unchanged');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy unchanged - Exception');
    failures++;
  }

  // Test 20: buffer.copy with range - first byte
  try {
    const src2 = Buffer.alloc(10);
    src2[0] = 0; src2[1] = 2; src2[2] = 4; src2[3] = 6; src2[4] = 8; src2[5] = 10;
    const dst2 = Buffer.alloc(10);
    src2.copy(dst2, 0, 3, 6);
    if (dst2[0] !== 6) {
      console.log('FAIL: buffer.copy with range - wrong first byte');
      failures++;
    } else {
      console.log('PASS: buffer.copy with range - first byte');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy with range (first) - Exception');
    failures++;
  }

  // Test 21: buffer.copy with range - second byte
  try {
    const src2 = Buffer.alloc(10);
    src2[0] = 0; src2[1] = 2; src2[2] = 4; src2[3] = 6; src2[4] = 8; src2[5] = 10;
    const dst2 = Buffer.alloc(10);
    src2.copy(dst2, 0, 3, 6);
    if (dst2[1] !== 8) {
      console.log('FAIL: buffer.copy with range - wrong second byte');
      failures++;
    } else {
      console.log('PASS: buffer.copy with range - second byte');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy with range (second) - Exception');
    failures++;
  }

  // Test 22: buffer.copy with range - third byte
  try {
    const src2 = Buffer.alloc(10);
    src2[0] = 0; src2[1] = 2; src2[2] = 4; src2[3] = 6; src2[4] = 8; src2[5] = 10;
    const dst2 = Buffer.alloc(10);
    src2.copy(dst2, 0, 3, 6);
    if (dst2[2] !== 10) {
      console.log('FAIL: buffer.copy with range - wrong third byte');
      failures++;
    } else {
      console.log('PASS: buffer.copy with range - third byte');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy with range (third) - Exception');
    failures++;
  }

  // Test 23: buffer.copy with range - rest unchanged
  try {
    const src2 = Buffer.alloc(10);
    src2[0] = 0; src2[1] = 2; src2[2] = 4; src2[3] = 6; src2[4] = 8; src2[5] = 10;
    const dst2 = Buffer.alloc(10);
    src2.copy(dst2, 0, 3, 6);
    if (dst2[3] !== 0) {
      console.log('FAIL: buffer.copy with range - rest changed');
      failures++;
    } else {
      console.log('PASS: buffer.copy with range - rest unchanged');
    }
  } catch (e) {
    console.log('FAIL: buffer.copy with range (rest) - Exception');
    failures++;
  }

  // Test 24: Buffer.concat length
  try {
    const a = Buffer.alloc(3);
    const b = Buffer.alloc(2);
    a[0] = 1; a[1] = 2; a[2] = 3;
    b[0] = 4; b[1] = 5;
    const concatenated = Buffer.concat([a, b]);
    if (concatenated.length !== 5) {
      console.log('FAIL: Buffer.concat - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.concat length is sum');
    }
  } catch (e) {
    console.log('FAIL: Buffer.concat length - Exception');
    failures++;
  }

  // Test 25: Buffer.concat values
  try {
    const a = Buffer.alloc(3);
    const b = Buffer.alloc(2);
    a[0] = 1; a[1] = 2; a[2] = 3;
    b[0] = 4; b[1] = 5;
    const concatenated = Buffer.concat([a, b]);
    if (!(concatenated[0] === 1 && concatenated[2] === 3 && concatenated[3] === 4 && concatenated[4] === 5)) {
      console.log('FAIL: Buffer.concat - wrong values');
      failures++;
    } else {
      console.log('PASS: Buffer.concat has correct values');
    }
  } catch (e) {
    console.log('FAIL: Buffer.concat values - Exception');
    failures++;
  }

  // Test 26: Buffer.isBuffer
  try {
    const buf1 = Buffer.alloc(10);
    if (Buffer.isBuffer(buf1) !== true) {
      console.log('FAIL: Buffer.isBuffer - returned false');
      failures++;
    } else {
      console.log('PASS: Buffer.isBuffer returns true for buffer');
    }
  } catch (e) {
    console.log('FAIL: Buffer.isBuffer - Exception');
    failures++;
  }

  // Test 27: buffer.slice with negative index - length
  try {
    const buf7 = Buffer.alloc(10);
    buf7[0] = 0; buf7[1] = 1; buf7[2] = 2; buf7[3] = 3; buf7[4] = 4;
    buf7[5] = 5; buf7[6] = 6; buf7[7] = 7; buf7[8] = 8; buf7[9] = 9;
    const negSlice = buf7.slice(-3, 10);
    if (negSlice.length !== 3) {
      console.log('FAIL: buffer.slice with negative index - wrong length');
      failures++;
    } else {
      console.log('PASS: buffer.slice with negative index - length');
    }
  } catch (e) {
    console.log('FAIL: buffer.slice with negative index (length) - Exception');
    failures++;
  }

  // Test 28: buffer.slice with negative index - values
  try {
    const buf7 = Buffer.alloc(10);
    buf7[0] = 0; buf7[1] = 1; buf7[2] = 2; buf7[3] = 3; buf7[4] = 4;
    buf7[5] = 5; buf7[6] = 6; buf7[7] = 7; buf7[8] = 8; buf7[9] = 9;
    const negSlice = buf7.slice(-3, 10);
    if (!(negSlice[0] === 7 && negSlice[2] === 9)) {
      console.log('FAIL: buffer.slice with negative index - wrong values');
      failures++;
    } else {
      console.log('PASS: buffer.slice with negative index - values');
    }
  } catch (e) {
    console.log('FAIL: buffer.slice with negative index (values) - Exception');
    failures++;
  }

  // Test 29: buffer.toString
  try {
    const strBuf = Buffer.from('world');
    const backToStr = strBuf.toString();
    if (typeof backToStr !== 'string' || backToStr.length === 0) {
      console.log('FAIL: buffer.toString - not a valid string');
      failures++;
    } else {
      console.log('PASS: buffer.toString converts to string');
    }
  } catch (e) {
    console.log('FAIL: buffer.toString - Exception');
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
