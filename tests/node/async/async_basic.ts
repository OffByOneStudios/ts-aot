// Async/Await Basic Tests

async function addAsync(a: number, b: number): Promise<number> {
  return a + b;
}

async function multiplyAsync(a: number, b: number): Promise<number> {
  return a * b;
}

async function user_main(): Promise<number> {
  let failures = 0;
  console.log('=== Async/Await Basic Tests ===\n');

  // Test 1: Simple async function call
  try {
    const sum = await addAsync(5, 10);
    if (sum !== 15) {
      console.log('FAIL: async function returned wrong value');
      failures++;
    } else {
      console.log('PASS: Simple async function');
    }
  } catch (e) {
    console.log('FAIL: Simple async function - Exception');
    failures++;
  }

  // Test 2: Multiple async function calls
  try {
    const sum = await addAsync(3, 4);
    const product = await multiplyAsync(sum, 2);
    if (product !== 14) {
      console.log('FAIL: Multiple async calls wrong value');
      failures++;
    } else {
      console.log('PASS: Multiple async function calls');
    }
  } catch (e) {
    console.log('FAIL: Multiple async calls - Exception');
    failures++;
  }

  // Test 3: Await with Promise.resolve
  try {
    const p = Promise.resolve(42);
    const val = await p;
    if (val !== 42) {
      console.log('FAIL: await Promise.resolve wrong value');
      failures++;
    } else {
      console.log('PASS: await Promise.resolve');
    }
  } catch (e) {
    console.log('FAIL: await Promise.resolve - Exception');
    failures++;
  }

  // Test 4: Async function returning string
  try {
    async function getString(): Promise<string> {
      return 'hello async';
    }
    const str = await getString();
    if (str.length !== 11) {
      console.log('FAIL: async string function wrong value');
      failures++;
    } else {
      console.log('PASS: Async function returning string');
    }
  } catch (e) {
    console.log('FAIL: async string function - Exception');
    failures++;
  }

  // Test 5: Sequential async operations
  try {
    const val1 = await addAsync(1, 1);
    const val2 = await addAsync(val1, 1);
    const val3 = await addAsync(val2, 1);
    if (val3 !== 4) {
      console.log('FAIL: Sequential async operations wrong value');
      failures++;
    } else {
      console.log('PASS: Sequential async operations');
    }
  } catch (e) {
    console.log('FAIL: Sequential async - Exception');
    failures++;
  }

  // Test 6: Async function with conditional
  try {
    async function conditionalAsync(x: number): Promise<number> {
      if (x > 10) {
        return x * 2;
      } else {
        return x + 10;
      }
    }
    const val1 = await conditionalAsync(5);
    const val2 = await conditionalAsync(15);
    if (val1 !== 15 || val2 !== 30) {
      console.log('FAIL: Async conditional wrong values');
      failures++;
    } else {
      console.log('PASS: Async function with conditional');
    }
  } catch (e) {
    console.log('FAIL: Async conditional - Exception');
    failures++;
  }

  // Test 7: Async function returning boolean
  try {
    async function checkAsync(x: number): Promise<boolean> {
      return x > 5;
    }
    const result1 = await checkAsync(10);
    const result2 = await checkAsync(3);
    if (!result1 || result2) {
      console.log('FAIL: Async boolean wrong values');
      failures++;
    } else {
      console.log('PASS: Async function returning boolean');
    }
  } catch (e) {
    console.log('FAIL: Async boolean - Exception');
    failures++;
  }

  // Test 8: Nested async calls
  try {
    async function outerAsync(): Promise<number> {
      const inner = await addAsync(2, 3);
      return await multiplyAsync(inner, 2);
    }
    const result = await outerAsync();
    if (result !== 10) {
      console.log('FAIL: Nested async calls wrong value');
      failures++;
    } else {
      console.log('PASS: Nested async calls');
    }
  } catch (e) {
    console.log('FAIL: Nested async - Exception');
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
