// Console Methods Test - TypeScript version

function user_main(): number {
  let failures = 0;
  console.log('=== Console Methods Tests (TS) ===\n');

  // Test 1: console.log (baseline)
  console.log('PASS: console.log works');

  // Test 2: console.error with string
  console.error('PASS: console.error with string');

  // Test 3: console.warn with string
  console.warn('PASS: console.warn with string');

  // Test 4: console.error with number
  console.error(42);

  // Test 5: console.warn with number
  console.warn(3.14);

  // Test 6: console.error with boolean
  console.error(true);

  // Test 7: console.warn with boolean
  console.warn(false);

  // Test 8: Mixed console output
  console.log('stdout: log');
  console.error('stderr: error');
  console.warn('stderr: warn');
  console.log('stdout: log again');

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
