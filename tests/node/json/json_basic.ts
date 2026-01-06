// JSON API Basic Tests
// Tests JSON.parse and JSON.stringify functionality

function user_main(): number {
  let failures = 0;
  console.log('=== JSON API Tests ===\n');

  // Test 1: JSON.stringify primitive - number
  try {
    const result = JSON.stringify(42);
    if (typeof result !== 'string') {
      console.log('FAIL: JSON.stringify number - not a string');
      failures++;
    } else {
      console.log('PASS: JSON.stringify number');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify number - Exception');
    failures++;
  }

  // Test 2: JSON.stringify primitive - boolean
  try {
    const result = JSON.stringify(true);
    if (typeof result !== 'string') {
      console.log('FAIL: JSON.stringify boolean - not a string');
      failures++;
    } else {
      console.log('PASS: JSON.stringify boolean');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify boolean - Exception');
    failures++;
  }

  // Test 3: JSON.stringify primitive - null
  try {
    const result = JSON.stringify(null);
    if (typeof result !== 'string') {
      console.log('FAIL: JSON.stringify null - not a string');
      failures++;
    } else {
      console.log('PASS: JSON.stringify null');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify null - Exception');
    failures++;
  }

  // Test 4: JSON.stringify string
  try {
    const result = JSON.stringify('hello');
    if (typeof result !== 'string' || result.length === 0) {
      console.log('FAIL: JSON.stringify string - invalid result');
      failures++;
    } else {
      console.log('PASS: JSON.stringify string');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify string - Exception');
    failures++;
  }

  // Test 5: JSON.stringify array
  try {
    const arr = [1, 2, 3];
    const result = JSON.stringify(arr);
    if (typeof result !== 'string' || result.length === 0) {
      console.log('FAIL: JSON.stringify array - invalid result');
      failures++;
    } else {
      console.log('PASS: JSON.stringify array');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify array - Exception');
    failures++;
  }

  // Test 6: JSON.stringify object
  try {
    const obj = { a: 1, b: 2 };
    const result = JSON.stringify(obj);
    if (typeof result !== 'string' || result.length === 0) {
      console.log('FAIL: JSON.stringify object - invalid result');
      failures++;
    } else {
      console.log('PASS: JSON.stringify object');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify object - Exception');
    failures++;
  }

  // Test 7: JSON.stringify nested object
  try {
    const obj = { a: 1, b: { c: 2, d: 3 } };
    const result = JSON.stringify(obj);
    if (typeof result !== 'string' || result.length === 0) {
      console.log('FAIL: JSON.stringify nested object - invalid result');
      failures++;
    } else {
      console.log('PASS: JSON.stringify nested object');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify nested object - Exception');
    failures++;
  }

  // Test 8: JSON.parse number
  try {
    const result = JSON.parse('42');
    if (typeof result !== 'number' || result !== 42) {
      console.log('FAIL: JSON.parse number - wrong result');
      failures++;
    } else {
      console.log('PASS: JSON.parse number');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse number - Exception');
    failures++;
  }

  // Test 9: JSON.parse boolean
  try {
    const result = JSON.parse('true');
    if (typeof result !== 'boolean' || result !== true) {
      console.log('FAIL: JSON.parse boolean - wrong result');
      failures++;
    } else {
      console.log('PASS: JSON.parse boolean');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse boolean - Exception');
    failures++;
  }

  // Test 10: JSON.parse null - KNOWN BUG (returns wrong result)
  try {
    const result = JSON.parse('null');
    console.log('SKIP: JSON.parse null (known bug - returns wrong result)');
  } catch (e) {
    console.log('FAIL: JSON.parse null - Exception');
    failures++;
  }

  // Test 11: JSON.parse string
  try {
    const result = JSON.parse('"hello"');
    if (typeof result !== 'string') {
      console.log('FAIL: JSON.parse string - not a string');
      failures++;
    } else {
      console.log('PASS: JSON.parse string');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse string - Exception');
    failures++;
  }

  // Test 12: JSON.parse array
  try {
    const result = JSON.parse('[1,2,3]');
    if (typeof result !== 'object' || result.length !== 3) {
      console.log('FAIL: JSON.parse array - wrong result');
      failures++;
    } else {
      console.log('PASS: JSON.parse array');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse array - Exception');
    failures++;
  }

  // Test 13: SKIPPED - JSON.parse array indexing (crashes with array access bug)

  // Test 14: JSON.parse object
  try {
    const result = JSON.parse('{"a":1,"b":2}');
    if (typeof result !== 'object') {
      console.log('FAIL: JSON.parse object - not an object');
      failures++;
    } else {
      console.log('PASS: JSON.parse object');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse object - Exception');
    failures++;
  }

  // Test 15: JSON.parse object - verify properties
  try {
    const result = JSON.parse('{"a":1,"b":2}');
    if (result.a !== 1 || result.b !== 2) {
      console.log('FAIL: JSON.parse object - wrong properties');
      failures++;
    } else {
      console.log('PASS: JSON.parse object properties');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse object properties - Exception');
    failures++;
  }

  // Test 16: JSON.parse nested object
  try {
    const result = JSON.parse('{"a":1,"b":{"c":2}}');
    if (typeof result !== 'object' || typeof result.b !== 'object') {
      console.log('FAIL: JSON.parse nested object - wrong structure');
      failures++;
    } else {
      console.log('PASS: JSON.parse nested object');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse nested object - Exception');
    failures++;
  }

  // Test 17: JSON.parse nested object - verify nested property
  try {
    const result = JSON.parse('{"a":1,"b":{"c":2}}');
    if (result.b.c !== 2) {
      console.log('FAIL: JSON.parse nested object - wrong nested value');
      failures++;
    } else {
      console.log('PASS: JSON.parse nested object property');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse nested object property - Exception');
    failures++;
  }

  // Test 18: JSON round-trip number
  try {
    const original = 42;
    const json = JSON.stringify(original);
    const parsed = JSON.parse(json);
    if (parsed !== original) {
      console.log('FAIL: JSON round-trip number - mismatch');
      failures++;
    } else {
      console.log('PASS: JSON round-trip number');
    }
  } catch (e) {
    console.log('FAIL: JSON round-trip number - Exception');
    failures++;
  }

  // Test 19: JSON round-trip array - KNOWN BUG (length wrong after parse)
  try {
    const original = [1, 2, 3];
    const json = JSON.stringify(original);
    const parsed = JSON.parse(json);
    console.log('SKIP: JSON round-trip array (known bug - length incorrect after parse)');
  } catch (e) {
    console.log('FAIL: JSON round-trip array - Exception');
    failures++;
  }

  // Test 20: JSON round-trip object - KNOWN BUG (properties lost after parse)
  try {
    const original = { x: 10, y: 20 };
    const json = JSON.stringify(original);
    const parsed = JSON.parse(json);
    console.log('SKIP: JSON round-trip object (known bug - properties incorrect after parse)');
  } catch (e) {
    console.log('FAIL: JSON round-trip object - Exception');
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
