// Object API Basic Tests - JavaScript version

function user_main() {
  var failures = 0;
  console.log('=== Object API Tests (JS) ===\n');

  // Test 1: Object.keys on object with properties
  var obj1 = { a: 1, b: 2, c: 3 };
  var keys1 = Object.keys(obj1);
  if (keys1.length !== 3) {
    console.log('FAIL: Object.keys - wrong length');
    failures = failures + 1;
  } else {
    console.log('PASS: Object.keys on object');
  }

  // Test 2: Object.keys on empty object
  var obj2 = {};
  var keys2 = Object.keys(obj2);
  if (keys2.length !== 0) {
    console.log('FAIL: Object.keys empty - wrong length');
    failures = failures + 1;
  } else {
    console.log('PASS: Object.keys on empty object');
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
