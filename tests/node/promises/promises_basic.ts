// Promise Basic Tests - Promise.resolve, Promise.reject, then, catch

async function user_main(): Promise<number> {
  let failures = 0;
  console.log('=== Promise Basic Tests ===\n');

  // Test 1: Promise.resolve
  try {
    const p = Promise.resolve(42);
    const val = await p;
    if (val !== 42) {
      console.log('FAIL: Promise.resolve value incorrect');
      failures++;
    } else {
      console.log('PASS: Promise.resolve');
    }
  } catch (e) {
    console.log('FAIL: Promise.resolve - Exception');
    failures++;
  }

  // Test 2: Promise.reject with catch
  try {
    const p = Promise.reject('test error');
    await p.catch((err) => {
      if (err.length > 0) {
        console.log('PASS: Promise.reject and catch');
      } else {
        console.log('FAIL: Promise.reject error value incorrect');
        failures++;
      }
    });
  } catch (e) {
    console.log('FAIL: Promise.reject - Exception');
    failures++;
  }

  // Test 3: Promise with then
  try {
    const p = Promise.resolve(10);
    const result = await p.then((x) => x + 5);
    if (result !== 15) {
      console.log('FAIL: Promise.then value incorrect');
      failures++;
    } else {
      console.log('PASS: Promise.then');
    }
  } catch (e) {
    console.log('FAIL: Promise.then - Exception');
    failures++;
  }

  // Test 4: Promise chaining
  try {
    const result = await Promise.resolve(1)
      .then((x) => x + 1)
      .then((x) => x + 1);
    if (result !== 3) {
      console.log('FAIL: Promise chaining value incorrect');
      failures++;
    } else {
      console.log('PASS: Promise chaining');
    }
  } catch (e) {
    console.log('FAIL: Promise chaining - Exception');
    failures++;
  }

  // Test 5: Promise constructor with executor
  try {
    const p = new Promise((resolve, reject) => {
      resolve(100);
    });
    const val = await p;
    if (val !== 100) {
      console.log('FAIL: Promise constructor value incorrect');
      failures++;
    } else {
      console.log('PASS: Promise constructor');
    }
  } catch (e) {
    console.log('FAIL: Promise constructor - Exception');
    failures++;
  }

  // Test 6: Promise error handling with try-catch
  try {
    let caught = false;
    try {
      await Promise.reject('expected error');
    } catch (err) {
      caught = true;
    }
    if (!caught) {
      console.log('FAIL: Promise error not caught');
      failures++;
    } else {
      console.log('PASS: Promise error handling');
    }
  } catch (e) {
    console.log('FAIL: Promise error handling - Exception');
    failures++;
  }

  // Test 7: Promise.resolve with non-promise value
  try {
    const p = Promise.resolve('hello');
    const val = await p;
    if (val.length !== 5) {
      console.log('FAIL: Promise.resolve string incorrect');
      failures++;
    } else {
      console.log('PASS: Promise.resolve with string');
    }
  } catch (e) {
    console.log('FAIL: Promise.resolve string - Exception');
    failures++;
  }

  // Test 8: Promise.then with multiple handlers
  try {
    const p = Promise.resolve(5);
    const val1 = await p.then((x) => x * 2);
    const val2 = await p.then((x) => x + 10);
    if (val1 !== 10 || val2 !== 15) {
      console.log('FAIL: Promise multiple then handlers incorrect');
      failures++;
    } else {
      console.log('PASS: Promise multiple then handlers');
    }
  } catch (e) {
    console.log('FAIL: Promise multiple then - Exception');
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
