// Buffer Encoding Tests
// Migrated from examples/buffer_encoding_test.ts
// Tests hex, base64, and base64url encoding/decoding

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer Encoding Tests ===\n');

  // Test 1: Hex encoding length
  try {
    const buf1 = Buffer.from('Hello');
    const hex1 = buf1.toString('hex');
    if (hex1.length !== 10) {
      console.log('FAIL: toString hex - wrong length');
      failures++;
    } else {
      console.log('PASS: toString hex encodes Hello to correct length');
    }
  } catch (e) {
    console.log('FAIL: toString hex - Exception');
    failures++;
  }

  // Test 2: Hex with bytes
  try {
    const buf2 = Buffer.alloc(3);
    buf2[0] = 0;
    buf2[1] = 255;
    buf2[2] = 128;
    const hex2 = buf2.toString('hex');
    if (hex2.length !== 6) {
      console.log('FAIL: toString hex bytes - wrong length');
      failures++;
    } else {
      console.log('PASS: toString hex encodes bytes to correct length');
    }
  } catch (e) {
    console.log('FAIL: toString hex bytes - Exception');
    failures++;
  }

  // Test 3: Buffer.from with hex encoding - length
  try {
    const buf3 = Buffer.from('48656c6c6f', 'hex');
    if (buf3.length !== 5) {
      console.log('FAIL: Buffer.from hex - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.from hex decodes to correct length');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from hex length - Exception');
    failures++;
  }

  // Test 4: Buffer.from hex decodes to correct bytes
  try {
    const buf3 = Buffer.from('48656c6c6f', 'hex');
    if (!(buf3[0] === 72 && buf3[4] === 111)) {
      console.log('FAIL: Buffer.from hex - wrong bytes');
      failures++;
    } else {
      console.log('PASS: Buffer.from hex decodes to correct bytes');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from hex bytes - Exception');
    failures++;
  }

  // Test 5: Buffer.from hex round-trips - length check
  try {
    const buf3 = Buffer.from('48656c6c6f', 'hex');
    const str3 = buf3.toString();
    if (str3.length !== 5) {
      console.log('FAIL: Buffer.from hex round-trip - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.from hex round-trips to correct length');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from hex round-trip - Exception');
    failures++;
  }

  // Test 6: Base64 encoding length
  try {
    const buf4 = Buffer.from('Hello');
    const b64_1 = buf4.toString('base64');
    if (b64_1.length !== 8) {
      console.log('FAIL: toString base64 - wrong length');
      failures++;
    } else {
      console.log('PASS: toString base64 encodes to correct length');
    }
  } catch (e) {
    console.log('FAIL: toString base64 - Exception');
    failures++;
  }

  // Test 7: Base64 decoding length
  try {
    const buf5 = Buffer.from('SGVsbG8=', 'base64');
    if (buf5.length !== 5) {
      console.log('FAIL: Buffer.from base64 - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.from base64 decodes to correct length');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from base64 length - Exception');
    failures++;
  }

  // Test 8: Base64 round-trips
  try {
    const buf5 = Buffer.from('SGVsbG8=', 'base64');
    const str5 = buf5.toString();
    if (str5.length !== 5) {
      console.log('FAIL: Buffer.from base64 round-trip - wrong length');
      failures++;
    } else {
      console.log('PASS: Buffer.from base64 round-trips correctly');
    }
  } catch (e) {
    console.log('FAIL: Buffer.from base64 round-trip - Exception');
    failures++;
  }

  // Test 9: Base64URL encoding (no padding)
  try {
    const buf6 = Buffer.from('Hello');
    const b64url = buf6.toString('base64url');
    if (b64url.length !== 7) {
      console.log('FAIL: toString base64url - wrong length');
      failures++;
    } else {
      console.log('PASS: toString base64url encodes without padding');
    }
  } catch (e) {
    console.log('FAIL: toString base64url - Exception');
    failures++;
  }

  // Test 10: Base64 padding test - single char
  try {
    const buf7 = Buffer.from('a');
    const b64_a = buf7.toString('base64');
    if (b64_a.length !== 4) {
      console.log('FAIL: toString base64 single char - wrong length');
      failures++;
    } else {
      console.log('PASS: toString base64 for single char has padding');
    }
  } catch (e) {
    console.log('FAIL: toString base64 single char - Exception');
    failures++;
  }

  // Test 11: Base64 padding test - two chars
  try {
    const buf8 = Buffer.from('ab');
    const b64_ab = buf8.toString('base64');
    if (b64_ab.length !== 4) {
      console.log('FAIL: toString base64 two chars - wrong length');
      failures++;
    } else {
      console.log('PASS: toString base64 for two chars has padding');
    }
  } catch (e) {
    console.log('FAIL: toString base64 two chars - Exception');
    failures++;
  }

  // Test 12: Base64 no padding test - three chars
  try {
    const buf9 = Buffer.from('abc');
    const b64_abc = buf9.toString('base64');
    if (b64_abc.length !== 4) {
      console.log('FAIL: toString base64 three chars - wrong length');
      failures++;
    } else {
      console.log('PASS: toString base64 for three chars no padding needed');
    }
  } catch (e) {
    console.log('FAIL: toString base64 three chars - Exception');
    failures++;
  }

  // Test 13: Base64 with special chars
  try {
    const buf10 = Buffer.alloc(2);
    buf10[0] = 251;
    buf10[1] = 239;
    const b64_special = buf10.toString('base64');
    if (typeof b64_special !== 'string' || b64_special.length === 0) {
      console.log('FAIL: toString base64 special chars - not a valid string');
      failures++;
    } else {
      console.log('PASS: toString base64 handles special byte values');
    }
  } catch (e) {
    console.log('FAIL: toString base64 special chars - Exception');
    failures++;
  }

  // Test 14: UTF-8 default encoding
  try {
    const buf11 = Buffer.from('world');
    const str11 = buf11.toString('utf8');
    if (str11.length !== 5) {
      console.log('FAIL: toString utf8 - wrong length');
      failures++;
    } else {
      console.log('PASS: toString utf8 still works');
    }
  } catch (e) {
    console.log('FAIL: toString utf8 - Exception');
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
