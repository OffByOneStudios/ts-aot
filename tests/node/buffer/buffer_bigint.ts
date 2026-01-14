// Buffer BigInt64 Read/Write Tests
// Tests readBigInt64BE/LE, readBigUInt64BE/LE, writeBigInt64BE/LE, writeBigUInt64BE/LE

function user_main(): number {
  let failures = 0;
  console.log('=== Buffer BigInt64 Tests ===\n');

  // Test 1: writeBigInt64LE and readBigInt64LE
  console.log('Test 1: writeBigInt64LE and readBigInt64LE');
  const buf1 = Buffer.alloc(8);
  const val1 = 72623859790382856n;  // 0x0102030405060708
  buf1.writeBigInt64LE(val1, 0);
  const read1 = buf1.readBigInt64LE(0);
  if (read1 === val1) {
    console.log('PASS: writeBigInt64LE/readBigInt64LE roundtrip');
  } else {
    console.log('FAIL: values do not match');
    failures++;
  }

  // Test 2: writeBigInt64BE and readBigInt64BE
  console.log('\nTest 2: writeBigInt64BE and readBigInt64BE');
  const buf2 = Buffer.alloc(8);
  const val2 = 72623859790382856n;  // 0x0102030405060708
  buf2.writeBigInt64BE(val2, 0);
  const read2 = buf2.readBigInt64BE(0);
  if (read2 === val2) {
    console.log('PASS: writeBigInt64BE/readBigInt64BE roundtrip');
  } else {
    console.log('FAIL: values do not match');
    failures++;
  }

  // Test 3: writeBigUInt64LE and readBigUInt64LE
  console.log('\nTest 3: writeBigUInt64LE and readBigUInt64LE');
  const buf3 = Buffer.alloc(8);
  const val3 = 1234567890123456789n;
  buf3.writeBigUInt64LE(val3, 0);
  const read3 = buf3.readBigUInt64LE(0);
  if (read3 === val3) {
    console.log('PASS: writeBigUInt64LE/readBigUInt64LE roundtrip');
  } else {
    console.log('FAIL: values do not match');
    failures++;
  }

  // Test 4: writeBigUInt64BE and readBigUInt64BE
  console.log('\nTest 4: writeBigUInt64BE and readBigUInt64BE');
  const buf4 = Buffer.alloc(8);
  const val4 = 81985529216486895n;  // 0x123456789ABCDEF
  buf4.writeBigUInt64BE(val4, 0);
  const read4 = buf4.readBigUInt64BE(0);
  if (read4 === val4) {
    console.log('PASS: writeBigUInt64BE/readBigUInt64BE roundtrip');
  } else {
    console.log('FAIL: values do not match');
    failures++;
  }

  // Test 5: Negative value with signed
  console.log('\nTest 5: Negative value with signed BigInt64');
  const buf5 = Buffer.alloc(8);
  const val5 = -1234567890123456789n;
  buf5.writeBigInt64LE(val5, 0);
  const read5 = buf5.readBigInt64LE(0);
  if (read5 === val5) {
    console.log('PASS: Negative BigInt64 roundtrip');
  } else {
    console.log('FAIL: negative values do not match');
    failures++;
  }

  // Test 6: Endianness check - verify BE and LE produce different bytes
  console.log('\nTest 6: Endianness verification');
  const bufLE = Buffer.alloc(8);
  const bufBE = Buffer.alloc(8);
  const testVal = 72623859790382856n;  // 0x0102030405060708
  bufLE.writeBigInt64LE(testVal, 0);
  bufBE.writeBigInt64BE(testVal, 0);
  // In LE, first byte should be 0x08, in BE first byte should be 0x01
  const leFirst = bufLE.readUInt8(0);
  const beFirst = bufBE.readUInt8(0);
  if (leFirst === 0x08 && beFirst === 0x01) {
    console.log('PASS: Endianness is correct');
  } else {
    console.log('FAIL: LE first byte=' + leFirst + ', BE first byte=' + beFirst);
    failures++;
  }

  // Test 7: Zero value
  console.log('\nTest 7: Zero value');
  const buf7 = Buffer.alloc(8);
  const val7 = 0n;
  buf7.writeBigInt64BE(val7, 0);
  const read7 = buf7.readBigInt64BE(0);
  if (read7 === 0n) {
    console.log('PASS: Zero value roundtrip');
  } else {
    console.log('FAIL: Zero value mismatch');
    failures++;
  }

  // Test 8: Write at offset
  console.log('\nTest 8: Write at offset');
  const buf8 = Buffer.alloc(16);
  const val8 = 723685415333310481n;  // 0x0A0B0C0D0E0F1011
  buf8.writeBigInt64LE(val8, 4);
  const read8 = buf8.readBigInt64LE(4);
  if (read8 === val8) {
    console.log('PASS: Write/read at offset works');
  } else {
    console.log('FAIL: offset write/read mismatch');
    failures++;
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All Buffer BigInt64 tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
