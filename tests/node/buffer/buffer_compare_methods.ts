// Buffer Compare and Search Methods Tests

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer Compare and Search Tests ===\n');

  // Test 1: buf.compare(other) - equal buffers
  console.log('Test 1: buf.compare(other) - equal buffers');
  const buf1a = Buffer.from('hello');
  const buf1b = Buffer.from('hello');
  const cmp1 = buf1a.compare(buf1b);
  if (cmp1 === 0) {
    console.log('PASS: compare returns 0 for equal buffers');
  } else {
    console.log('FAIL: compare returned ' + cmp1 + ' for equal buffers');
    failures++;
  }

  // Test 2: buf.compare(other) - less than
  console.log('\nTest 2: buf.compare(other) - less than');
  const buf2a = Buffer.from('abc');
  const buf2b = Buffer.from('abd');
  const cmp2 = buf2a.compare(buf2b);
  if (cmp2 < 0) {
    console.log('PASS: compare returns negative for lesser buffer');
  } else {
    console.log('FAIL: compare returned ' + cmp2 + ' (expected < 0)');
    failures++;
  }

  // Test 3: buf.compare(other) - greater than
  console.log('\nTest 3: buf.compare(other) - greater than');
  const buf3a = Buffer.from('abd');
  const buf3b = Buffer.from('abc');
  const cmp3 = buf3a.compare(buf3b);
  if (cmp3 > 0) {
    console.log('PASS: compare returns positive for greater buffer');
  } else {
    console.log('FAIL: compare returned ' + cmp3 + ' (expected > 0)');
    failures++;
  }

  // Test 4: buf.compare(other) - length difference
  console.log('\nTest 4: buf.compare(other) - length difference');
  const buf4a = Buffer.from('abc');
  const buf4b = Buffer.from('abcd');
  const cmp4 = buf4a.compare(buf4b);
  if (cmp4 < 0) {
    console.log('PASS: shorter buffer compares less');
  } else {
    console.log('FAIL: compare returned ' + cmp4 + ' (expected < 0)');
    failures++;
  }

  // Test 5: buf.equals(other) - equal
  console.log('\nTest 5: buf.equals(other) - equal buffers');
  const buf5a = Buffer.from('test');
  const buf5b = Buffer.from('test');
  if (buf5a.equals(buf5b)) {
    console.log('PASS: equals returns true for equal buffers');
  } else {
    console.log('FAIL: equals returned false for equal buffers');
    failures++;
  }

  // Test 6: buf.equals(other) - not equal
  console.log('\nTest 6: buf.equals(other) - not equal buffers');
  const buf6a = Buffer.from('test');
  const buf6b = Buffer.from('Test');
  if (!buf6a.equals(buf6b)) {
    console.log('PASS: equals returns false for different buffers');
  } else {
    console.log('FAIL: equals returned true for different buffers');
    failures++;
  }

  // Test 7: buf.indexOf(value) - found
  console.log('\nTest 7: buf.indexOf(value) - byte found');
  const buf7 = Buffer.from('hello');
  const idx7 = buf7.indexOf(0x6c); // 'l'
  if (idx7 === 2) {
    console.log('PASS: indexOf found byte at correct position');
  } else {
    console.log('FAIL: indexOf returned ' + idx7 + ' (expected 2)');
    failures++;
  }

  // Test 8: buf.indexOf(value) - not found
  console.log('\nTest 8: buf.indexOf(value) - byte not found');
  const buf8 = Buffer.from('hello');
  const idx8 = buf8.indexOf(0x7a); // 'z'
  if (idx8 === -1) {
    console.log('PASS: indexOf returns -1 for not found');
  } else {
    console.log('FAIL: indexOf returned ' + idx8 + ' (expected -1)');
    failures++;
  }

  // Test 9: buf.indexOf(value, byteOffset)
  console.log('\nTest 9: buf.indexOf(value, byteOffset)');
  const buf9 = Buffer.from('hello');
  const idx9 = buf9.indexOf(0x6c, 3); // 'l' starting from offset 3
  if (idx9 === 3) {
    console.log('PASS: indexOf with offset works');
  } else {
    console.log('FAIL: indexOf with offset returned ' + idx9 + ' (expected 3)');
    failures++;
  }

  // Test 10: buf.lastIndexOf(value)
  console.log('\nTest 10: buf.lastIndexOf(value)');
  const buf10 = Buffer.from('hello');
  const idx10 = buf10.lastIndexOf(0x6c); // 'l' - last occurrence
  if (idx10 === 3) {
    console.log('PASS: lastIndexOf finds last occurrence');
  } else {
    console.log('FAIL: lastIndexOf returned ' + idx10 + ' (expected 3)');
    failures++;
  }

  // Test 11: buf.includes(value) - found
  console.log('\nTest 11: buf.includes(value) - found');
  const buf11 = Buffer.from('hello');
  if (buf11.includes(0x65)) { // 'e'
    console.log('PASS: includes returns true when byte exists');
  } else {
    console.log('FAIL: includes returned false when byte exists');
    failures++;
  }

  // Test 12: buf.includes(value) - not found
  console.log('\nTest 12: buf.includes(value) - not found');
  const buf12 = Buffer.from('hello');
  if (!buf12.includes(0x7a)) { // 'z'
    console.log('PASS: includes returns false when byte not found');
  } else {
    console.log('FAIL: includes returned true when byte not found');
    failures++;
  }

  // Test 13: Buffer.compare(buf1, buf2) - static method
  console.log('\nTest 13: Buffer.compare(buf1, buf2) - static');
  const buf13a = Buffer.from('abc');
  const buf13b = Buffer.from('abc');
  const cmp13 = Buffer.compare(buf13a, buf13b);
  if (cmp13 === 0) {
    console.log('PASS: Buffer.compare works');
  } else {
    console.log('FAIL: Buffer.compare returned ' + cmp13 + ' (expected 0)');
    failures++;
  }

  // Test 14: Buffer.isEncoding
  console.log('\nTest 14: Buffer.isEncoding');
  if (Buffer.isEncoding('utf8') && Buffer.isEncoding('utf-8') &&
      Buffer.isEncoding('hex') && Buffer.isEncoding('base64') &&
      !Buffer.isEncoding('invalid')) {
    console.log('PASS: Buffer.isEncoding works');
  } else {
    console.log('FAIL: Buffer.isEncoding returned unexpected results');
    console.log('  utf8: ' + Buffer.isEncoding('utf8'));
    console.log('  utf-8: ' + Buffer.isEncoding('utf-8'));
    console.log('  hex: ' + Buffer.isEncoding('hex'));
    console.log('  base64: ' + Buffer.isEncoding('base64'));
    console.log('  invalid: ' + Buffer.isEncoding('invalid'));
    failures++;
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All Buffer compare/search tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
