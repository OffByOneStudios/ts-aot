// Async/Await Basic Tests - JavaScript version

async function addAsync(a, b) {
  return a + b;
}

async function multiplyAsync(a, b) {
  return a * b;
}

async function user_main() {
  var failures = 0;
  console.log('=== Async/Await Basic Tests (JS) ===\n');

  // Test 1: Simple async function call
  try {
    var sum = await addAsync(5, 10);
    if (sum !== 15) {
      console.log('FAIL: async function returned wrong value');
      failures = failures + 1;
    } else {
      console.log('PASS: Simple async function');
    }
  } catch (e) {
    console.log('FAIL: Simple async function - Exception');
    failures = failures + 1;
  }

  // Test 2: Multiple async function calls
  try {
    var sum2 = await addAsync(3, 4);
    var product = await multiplyAsync(sum2, 2);
    if (product !== 14) {
      console.log('FAIL: Multiple async calls wrong value');
      failures = failures + 1;
    } else {
      console.log('PASS: Multiple async function calls');
    }
  } catch (e) {
    console.log('FAIL: Multiple async calls - Exception');
    failures = failures + 1;
  }

  // Test 3: Await with Promise.resolve
  try {
    var p = Promise.resolve(42);
    var val = await p;
    if (val !== 42) {
      console.log('FAIL: await Promise.resolve wrong value');
      failures = failures + 1;
    } else {
      console.log('PASS: await Promise.resolve');
    }
  } catch (e) {
    console.log('FAIL: await Promise.resolve - Exception');
    failures = failures + 1;
  }

  // Test 4: Sequential async operations
  try {
    var val1 = await addAsync(1, 1);
    var val2 = await addAsync(val1, 1);
    var val3 = await addAsync(val2, 1);
    if (val3 !== 4) {
      console.log('FAIL: Sequential async operations wrong value');
      failures = failures + 1;
    } else {
      console.log('PASS: Sequential async operations');
    }
  } catch (e) {
    console.log('FAIL: Sequential async - Exception');
    failures = failures + 1;
  }

  // Test 5: Nested async calls
  try {
    async function outerAsync() {
      var inner = await addAsync(2, 3);
      return await multiplyAsync(inner, 2);
    }
    var result = await outerAsync();
    if (result !== 10) {
      console.log('FAIL: Nested async calls wrong value');
      failures = failures + 1;
    } else {
      console.log('PASS: Nested async calls');
    }
  } catch (e) {
    console.log('FAIL: Nested async - Exception');
    failures = failures + 1;
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
