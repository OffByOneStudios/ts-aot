// Closure Increment/Decrement Test
// Tests that ++/-- operators work correctly for captured mutable variables in closures.
// This is a regression test for a compiler bug where cell variables weren't updated.
//
// Related golden IR test: tests/golden_ir/typescript/regression/closure_increment.ts

function user_main(): number {
  let failures = 0;
  console.log('=== Closure Increment/Decrement Tests ===\n');

  // Test 1: Assignment pattern (baseline - always worked)
  try {
    let count = 0;
    const fn = () => { count = count + 1; };
    fn();
    fn();
    if (count !== 2) {
      console.log('FAIL: Assignment pattern - expected 2, got ' + count);
      failures++;
    } else {
      console.log('PASS: Assignment pattern (count = count + 1)');
    }
  } catch (e) {
    console.log('FAIL: Assignment pattern - Exception');
    failures++;
  }

  // Test 2: Postfix increment (count++)
  try {
    let count = 0;
    const fn = () => { count++; };
    fn();
    fn();
    if (count !== 2) {
      console.log('FAIL: Postfix increment - expected 2, got ' + count);
      failures++;
    } else {
      console.log('PASS: Postfix increment (count++)');
    }
  } catch (e) {
    console.log('FAIL: Postfix increment - Exception');
    failures++;
  }

  // Test 3: Prefix increment (++count)
  try {
    let count = 0;
    const fn = () => { ++count; };
    fn();
    fn();
    if (count !== 2) {
      console.log('FAIL: Prefix increment - expected 2, got ' + count);
      failures++;
    } else {
      console.log('PASS: Prefix increment (++count)');
    }
  } catch (e) {
    console.log('FAIL: Prefix increment - Exception');
    failures++;
  }

  // Test 4: Postfix decrement (count--)
  try {
    let count = 2;
    const fn = () => { count--; };
    fn();
    fn();
    if (count !== 0) {
      console.log('FAIL: Postfix decrement - expected 0, got ' + count);
      failures++;
    } else {
      console.log('PASS: Postfix decrement (count--)');
    }
  } catch (e) {
    console.log('FAIL: Postfix decrement - Exception');
    failures++;
  }

  // Test 5: Prefix decrement (--count)
  try {
    let count = 2;
    const fn = () => { --count; };
    fn();
    fn();
    if (count !== 0) {
      console.log('FAIL: Prefix decrement - expected 0, got ' + count);
      failures++;
    } else {
      console.log('PASS: Prefix decrement (--count)');
    }
  } catch (e) {
    console.log('FAIL: Prefix decrement - Exception');
    failures++;
  }

  // Test 6: Postfix increment return value
  try {
    let count = 5;
    let returnedValue = 0;
    const fn = () => { returnedValue = count++; };
    fn();
    // count++ should return old value (5), then increment to 6
    if (returnedValue !== 5 || count !== 6) {
      console.log('FAIL: Postfix return value - expected ret=5 count=6, got ret=' + returnedValue + ' count=' + count);
      failures++;
    } else {
      console.log('PASS: Postfix increment return value');
    }
  } catch (e) {
    console.log('FAIL: Postfix return value - Exception');
    failures++;
  }

  // Test 7: Prefix increment return value
  try {
    let count = 5;
    let returnedValue = 0;
    const fn = () => { returnedValue = ++count; };
    fn();
    // ++count should increment to 6, then return new value (6)
    if (returnedValue !== 6 || count !== 6) {
      console.log('FAIL: Prefix return value - expected ret=6 count=6, got ret=' + returnedValue + ' count=' + count);
      failures++;
    } else {
      console.log('PASS: Prefix increment return value');
    }
  } catch (e) {
    console.log('FAIL: Prefix return value - Exception');
    failures++;
  }

  // Test 8: Multiple closures sharing same variable
  try {
    let shared = 0;
    const inc = () => { shared++; };
    const dec = () => { shared--; };
    inc();
    inc();
    inc();
    dec();
    if (shared !== 2) {
      console.log('FAIL: Multiple closures - expected 2, got ' + shared);
      failures++;
    } else {
      console.log('PASS: Multiple closures sharing variable');
    }
  } catch (e) {
    console.log('FAIL: Multiple closures - Exception');
    failures++;
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
