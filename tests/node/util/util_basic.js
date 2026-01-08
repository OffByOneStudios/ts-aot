// Util Module Basic Tests - JavaScript version

var util = require("util");

function user_main() {
  var failures = 0;
  console.log('=== Util Module Tests (JS) ===\n');

  // Test 1: util.format with %s
  try {
    var formatted = util.format("Hello %s", "World");
    if (typeof formatted !== 'string') {
      console.log('FAIL: util.format should return string');
      failures = failures + 1;
    } else {
      console.log('PASS: util.format with %s');
    }
  } catch (e) {
    console.log('FAIL: util.format %s - Exception');
    failures = failures + 1;
  }

  // Test 2: util.format with %d
  try {
    var formatted2 = util.format("Count: %d", 42);
    if (typeof formatted2 !== 'string') {
      console.log('FAIL: util.format should return string');
      failures = failures + 1;
    } else {
      console.log('PASS: util.format with %d');
    }
  } catch (e) {
    console.log('FAIL: util.format %d - Exception');
    failures = failures + 1;
  }

  // Test 3: util.format with multiple placeholders
  try {
    var formatted3 = util.format("%s has %d items", "Cart", 5);
    if (typeof formatted3 !== 'string') {
      console.log('FAIL: util.format should return string');
      failures = failures + 1;
    } else {
      console.log('PASS: util.format multiple placeholders');
    }
  } catch (e) {
    console.log('FAIL: util.format multiple - Exception');
    failures = failures + 1;
  }

  // Test 4: util.inspect with string
  try {
    var inspected = util.inspect("hello");
    if (typeof inspected !== 'string') {
      console.log('FAIL: util.inspect should return string');
      failures = failures + 1;
    } else {
      console.log('PASS: util.inspect string');
    }
  } catch (e) {
    console.log('FAIL: util.inspect - Exception');
    failures = failures + 1;
  }

  // Test 5: util.inspect with number
  try {
    var inspected2 = util.inspect(42);
    if (typeof inspected2 !== 'string') {
      console.log('FAIL: util.inspect should return string');
      failures = failures + 1;
    } else {
      console.log('PASS: util.inspect number');
    }
  } catch (e) {
    console.log('FAIL: util.inspect number - Exception');
    failures = failures + 1;
  }

  // Test 6: util.inspect with object
  try {
    var inspected3 = util.inspect({ a: 1, b: 2 });
    if (typeof inspected3 !== 'string') {
      console.log('FAIL: util.inspect should return string');
      failures = failures + 1;
    } else {
      console.log('PASS: util.inspect object');
    }
  } catch (e) {
    console.log('FAIL: util.inspect object - Exception');
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
