// JSON API Basic Tests - JavaScript version

function user_main() {
  var failures = 0;
  console.log('=== JSON API Tests (JS) ===\n');

  // Test 1: JSON.stringify number
  try {
    var result = JSON.stringify(42);
    if (typeof result !== 'string') {
      console.log('FAIL: JSON.stringify number - not a string');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.stringify number');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify number - Exception');
    failures = failures + 1;
  }

  // Test 2: JSON.stringify boolean
  try {
    var result2 = JSON.stringify(true);
    if (typeof result2 !== 'string') {
      console.log('FAIL: JSON.stringify boolean - not a string');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.stringify boolean');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify boolean - Exception');
    failures = failures + 1;
  }

  // Test 3: JSON.stringify string
  try {
    var result3 = JSON.stringify('hello');
    if (typeof result3 !== 'string' || result3.length === 0) {
      console.log('FAIL: JSON.stringify string - invalid result');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.stringify string');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify string - Exception');
    failures = failures + 1;
  }

  // Test 4: JSON.stringify array
  try {
    var arr = [1, 2, 3];
    var result4 = JSON.stringify(arr);
    if (typeof result4 !== 'string' || result4.length === 0) {
      console.log('FAIL: JSON.stringify array - invalid result');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.stringify array');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify array - Exception');
    failures = failures + 1;
  }

  // Test 5: JSON.stringify object
  try {
    var obj = { a: 1, b: 2 };
    var result5 = JSON.stringify(obj);
    if (typeof result5 !== 'string' || result5.length === 0) {
      console.log('FAIL: JSON.stringify object - invalid result');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.stringify object');
    }
  } catch (e) {
    console.log('FAIL: JSON.stringify object - Exception');
    failures = failures + 1;
  }

  // Test 6: JSON.parse number
  try {
    var parsed = JSON.parse('42');
    if (typeof parsed !== 'number' || parsed !== 42) {
      console.log('FAIL: JSON.parse number - wrong result');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.parse number');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse number - Exception');
    failures = failures + 1;
  }

  // Test 7: JSON.parse boolean
  try {
    var parsed2 = JSON.parse('true');
    if (typeof parsed2 !== 'boolean' || parsed2 !== true) {
      console.log('FAIL: JSON.parse boolean - wrong result');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.parse boolean');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse boolean - Exception');
    failures = failures + 1;
  }

  // Test 8: JSON.parse string
  try {
    var parsed3 = JSON.parse('"hello"');
    if (typeof parsed3 !== 'string') {
      console.log('FAIL: JSON.parse string - not a string');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.parse string');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse string - Exception');
    failures = failures + 1;
  }

  // Test 9: JSON.parse object
  try {
    var parsed4 = JSON.parse('{"a":1,"b":2}');
    if (typeof parsed4 !== 'object') {
      console.log('FAIL: JSON.parse object - not an object');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.parse object');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse object - Exception');
    failures = failures + 1;
  }

  // Test 10: JSON.parse object properties
  try {
    var parsed5 = JSON.parse('{"a":1,"b":2}');
    if (parsed5.a !== 1 || parsed5.b !== 2) {
      console.log('FAIL: JSON.parse object - wrong properties');
      failures = failures + 1;
    } else {
      console.log('PASS: JSON.parse object properties');
    }
  } catch (e) {
    console.log('FAIL: JSON.parse object properties - Exception');
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
