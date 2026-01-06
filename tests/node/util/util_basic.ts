// Util Module Basic Tests
import * as util from "util";

function user_main(): number {
  let failures = 0;
  console.log('=== Util Module Tests ===\n');

  // Test 1: util.format with %s
  try {
    const formatted = util.format("Hello %s", "World");
    if (typeof formatted !== 'string') {
      console.log('FAIL: util.format should return string');
      failures++;
    } else {
      console.log('PASS: util.format with %s');
      console.log('  Result:', formatted);
    }
  } catch (e) {
    console.log('FAIL: util.format %s - Exception');
    failures++;
  }

  // Test 2: util.format with %d
  try {
    const formatted = util.format("Count: %d", 42);
    if (typeof formatted !== 'string') {
      console.log('FAIL: util.format should return string');
      failures++;
    } else {
      console.log('PASS: util.format with %d');
      console.log('  Result:', formatted);
    }
  } catch (e) {
    console.log('FAIL: util.format %d - Exception');
    failures++;
  }

  // Test 3: util.format with multiple placeholders
  try {
    const formatted = util.format("%s has %d items", "Cart", 5);
    if (typeof formatted !== 'string') {
      console.log('FAIL: util.format should return string');
      failures++;
    } else {
      console.log('PASS: util.format multiple placeholders');
      console.log('  Result:', formatted);
    }
  } catch (e) {
    console.log('FAIL: util.format multiple - Exception');
    failures++;
  }

  // Test 4: util.inspect with string
  try {
    const inspected = util.inspect("hello");
    if (typeof inspected !== 'string') {
      console.log('FAIL: util.inspect should return string');
      failures++;
    } else {
      console.log('PASS: util.inspect string');
      console.log('  Result:', inspected);
    }
  } catch (e) {
    console.log('FAIL: util.inspect - Exception');
    failures++;
  }

  // Test 5: util.inspect with number
  try {
    const inspected = util.inspect(42);
    if (typeof inspected !== 'string') {
      console.log('FAIL: util.inspect should return string');
      failures++;
    } else {
      console.log('PASS: util.inspect number');
      console.log('  Result:', inspected);
    }
  } catch (e) {
    console.log('FAIL: util.inspect number - Exception');
    failures++;
  }

  // Test 6: util.inspect with object
  try {
    const inspected = util.inspect({ a: 1, b: 2 });
    if (typeof inspected !== 'string') {
      console.log('FAIL: util.inspect should return string');
      failures++;
    } else {
      console.log('PASS: util.inspect object');
      console.log('  Result:', inspected);
    }
  } catch (e) {
    console.log('FAIL: util.inspect object - Exception');
    failures++;
  }

  // Test 7: util.types.isMap
  try {
    const map = new Map();
    const result = util.types.isMap(map);
    if (typeof result !== 'boolean') {
      console.log('FAIL: util.types.isMap should return boolean');
      failures++;
    } else {
      console.log('PASS: util.types.isMap');
      console.log('  Result:', result);
    }
  } catch (e) {
    console.log('FAIL: util.types.isMap - Exception');
    failures++;
  }

  // Test 8: util.types.isSet
  try {
    const set = new Set();
    const result = util.types.isSet(set);
    if (typeof result !== 'boolean') {
      console.log('FAIL: util.types.isSet should return boolean');
      failures++;
    } else {
      console.log('PASS: util.types.isSet');
      console.log('  Result:', result);
    }
  } catch (e) {
    console.log('FAIL: util.types.isSet - Exception');
    failures++;
  }

  // Test 9: util.types.isDate
  try {
    const date = new Date();
    const result = util.types.isDate(date);
    if (typeof result !== 'boolean') {
      console.log('FAIL: util.types.isDate should return boolean');
      failures++;
    } else {
      console.log('PASS: util.types.isDate');
      console.log('  Result:', result);
    }
  } catch (e) {
    console.log('FAIL: util.types.isDate - Exception');
    failures++;
  }

  // Test 10: util.types.isTypedArray with Buffer
  try {
    const buf = Buffer.from("hello");
    const result = util.types.isTypedArray(buf);
    if (typeof result !== 'boolean') {
      console.log('FAIL: util.types.isTypedArray should return boolean');
      failures++;
    } else {
      console.log('PASS: util.types.isTypedArray');
      console.log('  Result:', result);
    }
  } catch (e) {
    console.log('FAIL: util.types.isTypedArray - Exception');
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
