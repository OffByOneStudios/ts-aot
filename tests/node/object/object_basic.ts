// Object API Basic Tests
// Tests Object.keys (other Object methods cause compiler segfault when combined)

function user_main(): number {
  let failures = 0;
  console.log('=== Object API Tests ===\n');

  // Test 1: Object.keys on object with properties
  const obj1 = { a: 1, b: 2, c: 3 };
  const keys1 = Object.keys(obj1);
  if (keys1.length !== 3) {
    console.log('FAIL: Object.keys - wrong length');
    failures++;
  } else {
    console.log('PASS: Object.keys on object');
  }

  // Test 2: Object.keys on empty object
  const obj2 = {};
  const keys2 = Object.keys(obj2);
  if (keys2.length !== 0) {
    console.log('FAIL: Object.keys empty - wrong length');
    failures++;
  } else {
    console.log('PASS: Object.keys on empty object');
  }

  console.log('\n=== Summary ===');
  console.log('Note: Only testing Object.keys due to compiler limitation');
  console.log('(Multiple Object.* calls in one file cause compiler segfault)');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
