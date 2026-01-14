// Buffer Read/Write Methods Tests

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer Read/Write Tests ===\n');

  // Test 1: Write and Read Int8
  console.log('Test 1: writeInt8 / readInt8');
  const buf1 = Buffer.alloc(4);
  buf1.writeInt8(127, 0);
  buf1.writeInt8(-128, 1);
  buf1.writeInt8(0, 2);
  buf1.writeInt8(-1, 3);
  if (buf1.readInt8(0) === 127 && buf1.readInt8(1) === -128 &&
      buf1.readInt8(2) === 0 && buf1.readInt8(3) === -1) {
    console.log('PASS: Int8 read/write works');
  } else {
    console.log('FAIL: Int8 read/write failed');
    console.log('  Expected: 127, -128, 0, -1');
    console.log('  Got: ' + buf1.readInt8(0) + ', ' + buf1.readInt8(1) + ', ' + buf1.readInt8(2) + ', ' + buf1.readInt8(3));
    failures++;
  }

  // Test 2: Write and Read UInt8
  console.log('\nTest 2: writeUInt8 / readUInt8');
  const buf2 = Buffer.alloc(3);
  buf2.writeUInt8(0, 0);
  buf2.writeUInt8(128, 1);
  buf2.writeUInt8(255, 2);
  if (buf2.readUInt8(0) === 0 && buf2.readUInt8(1) === 128 && buf2.readUInt8(2) === 255) {
    console.log('PASS: UInt8 read/write works');
  } else {
    console.log('FAIL: UInt8 read/write failed');
    console.log('  Expected: 0, 128, 255');
    console.log('  Got: ' + buf2.readUInt8(0) + ', ' + buf2.readUInt8(1) + ', ' + buf2.readUInt8(2));
    failures++;
  }

  // Test 3: Write and Read Int16LE
  console.log('\nTest 3: writeInt16LE / readInt16LE');
  const buf3 = Buffer.alloc(4);
  buf3.writeInt16LE(0x1234, 0);
  buf3.writeInt16LE(-1, 2);
  if (buf3.readInt16LE(0) === 0x1234 && buf3.readInt16LE(2) === -1) {
    console.log('PASS: Int16LE read/write works');
  } else {
    console.log('FAIL: Int16LE read/write failed');
    console.log('  Expected: 4660, -1');
    console.log('  Got: ' + buf3.readInt16LE(0) + ', ' + buf3.readInt16LE(2));
    failures++;
  }

  // Test 4: Write and Read Int16BE
  console.log('\nTest 4: writeInt16BE / readInt16BE');
  const buf4 = Buffer.alloc(4);
  buf4.writeInt16BE(0x1234, 0);
  buf4.writeInt16BE(-256, 2);
  if (buf4.readInt16BE(0) === 0x1234 && buf4.readInt16BE(2) === -256) {
    console.log('PASS: Int16BE read/write works');
  } else {
    console.log('FAIL: Int16BE read/write failed');
    console.log('  Expected: 4660, -256');
    console.log('  Got: ' + buf4.readInt16BE(0) + ', ' + buf4.readInt16BE(2));
    failures++;
  }

  // Test 5: Write and Read UInt16LE
  console.log('\nTest 5: writeUInt16LE / readUInt16LE');
  const buf5 = Buffer.alloc(4);
  buf5.writeUInt16LE(0, 0);
  buf5.writeUInt16LE(65535, 2);
  if (buf5.readUInt16LE(0) === 0 && buf5.readUInt16LE(2) === 65535) {
    console.log('PASS: UInt16LE read/write works');
  } else {
    console.log('FAIL: UInt16LE read/write failed');
    console.log('  Expected: 0, 65535');
    console.log('  Got: ' + buf5.readUInt16LE(0) + ', ' + buf5.readUInt16LE(2));
    failures++;
  }

  // Test 6: Write and Read Int32LE
  console.log('\nTest 6: writeInt32LE / readInt32LE');
  const buf6 = Buffer.alloc(8);
  buf6.writeInt32LE(0x12345678, 0);
  buf6.writeInt32LE(-1, 4);
  if (buf6.readInt32LE(0) === 0x12345678 && buf6.readInt32LE(4) === -1) {
    console.log('PASS: Int32LE read/write works');
  } else {
    console.log('FAIL: Int32LE read/write failed');
    console.log('  Expected: 305419896, -1');
    console.log('  Got: ' + buf6.readInt32LE(0) + ', ' + buf6.readInt32LE(4));
    failures++;
  }

  // Test 7: Write and Read Int32BE
  console.log('\nTest 7: writeInt32BE / readInt32BE');
  const buf7 = Buffer.alloc(8);
  buf7.writeInt32BE(0x12345678, 0);
  buf7.writeInt32BE(-256, 4);
  if (buf7.readInt32BE(0) === 0x12345678 && buf7.readInt32BE(4) === -256) {
    console.log('PASS: Int32BE read/write works');
  } else {
    console.log('FAIL: Int32BE read/write failed');
    console.log('  Expected: 305419896, -256');
    console.log('  Got: ' + buf7.readInt32BE(0) + ', ' + buf7.readInt32BE(4));
    failures++;
  }

  // Test 8: Write and Read UInt32LE
  console.log('\nTest 8: writeUInt32LE / readUInt32LE');
  const buf8 = Buffer.alloc(8);
  buf8.writeUInt32LE(0, 0);
  buf8.writeUInt32LE(0xFFFFFFFF, 4);
  if (buf8.readUInt32LE(0) === 0 && buf8.readUInt32LE(4) === 0xFFFFFFFF) {
    console.log('PASS: UInt32LE read/write works');
  } else {
    console.log('FAIL: UInt32LE read/write failed');
    console.log('  Expected: 0, 4294967295');
    console.log('  Got: ' + buf8.readUInt32LE(0) + ', ' + buf8.readUInt32LE(4));
    failures++;
  }

  // Test 9: Write and Read FloatLE
  console.log('\nTest 9: writeFloatLE / readFloatLE');
  const buf9 = Buffer.alloc(4);
  buf9.writeFloatLE(3.14, 0);
  const floatVal = buf9.readFloatLE(0);
  // Float precision - check if close enough (within 0.001)
  if (Math.abs(floatVal - 3.14) < 0.001) {
    console.log('PASS: FloatLE read/write works');
  } else {
    console.log('FAIL: FloatLE read/write failed');
    console.log('  Expected: ~3.14');
    console.log('  Got: ' + floatVal);
    failures++;
  }

  // Test 10: Write and Read FloatBE
  console.log('\nTest 10: writeFloatBE / readFloatBE');
  const buf10 = Buffer.alloc(4);
  buf10.writeFloatBE(-2.5, 0);
  const floatBEVal = buf10.readFloatBE(0);
  if (Math.abs(floatBEVal - (-2.5)) < 0.001) {
    console.log('PASS: FloatBE read/write works');
  } else {
    console.log('FAIL: FloatBE read/write failed');
    console.log('  Expected: ~-2.5');
    console.log('  Got: ' + floatBEVal);
    failures++;
  }

  // Test 11: Write and Read DoubleLE
  console.log('\nTest 11: writeDoubleLE / readDoubleLE');
  const buf11 = Buffer.alloc(8);
  buf11.writeDoubleLE(Math.PI, 0);
  const doubleVal = buf11.readDoubleLE(0);
  if (Math.abs(doubleVal - Math.PI) < 0.0000001) {
    console.log('PASS: DoubleLE read/write works');
  } else {
    console.log('FAIL: DoubleLE read/write failed');
    console.log('  Expected: ~' + Math.PI);
    console.log('  Got: ' + doubleVal);
    failures++;
  }

  // Test 12: Write and Read DoubleBE
  console.log('\nTest 12: writeDoubleBE / readDoubleBE');
  const buf12 = Buffer.alloc(8);
  buf12.writeDoubleBE(-123.456789, 0);
  const doubleBEVal = buf12.readDoubleBE(0);
  if (Math.abs(doubleBEVal - (-123.456789)) < 0.0000001) {
    console.log('PASS: DoubleBE read/write works');
  } else {
    console.log('FAIL: DoubleBE read/write failed');
    console.log('  Expected: ~-123.456789');
    console.log('  Got: ' + doubleBEVal);
    failures++;
  }

  // Test 13: Write returns offset after written bytes
  console.log('\nTest 13: write methods return offset after written bytes');
  const buf13 = Buffer.alloc(16);
  let offset = 0;
  offset = buf13.writeUInt8(1, offset);    // offset should be 1
  offset = buf13.writeUInt16LE(2, offset); // offset should be 3
  offset = buf13.writeUInt32LE(3, offset); // offset should be 7
  if (offset === 7) {
    console.log('PASS: Write returns correct offset');
  } else {
    console.log('FAIL: Write returns wrong offset');
    console.log('  Expected: 7');
    console.log('  Got: ' + offset);
    failures++;
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All Buffer read/write tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
