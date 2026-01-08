// Promise Basic Tests - JavaScript version

async function user_main() {
  var failures = 0;
  console.log('=== Promise Basic Tests (JS) ===\n');

  // Test 1: Promise.resolve
  try {
    var p = Promise.resolve(42);
    var val = await p;
    if (val !== 42) {
      console.log('FAIL: Promise.resolve value incorrect');
      failures = failures + 1;
    } else {
      console.log('PASS: Promise.resolve');
    }
  } catch (e) {
    console.log('FAIL: Promise.resolve - Exception');
    failures = failures + 1;
  }

  // Test 2: Promise.reject with catch
  try {
    var p2 = Promise.reject('test error');
    await p2.catch(function(err) {
      if (err.length > 0) {
        console.log('PASS: Promise.reject and catch');
      } else {
        console.log('FAIL: Promise.reject error value incorrect');
        failures = failures + 1;
      }
    });
  } catch (e) {
    console.log('FAIL: Promise.reject - Exception');
    failures = failures + 1;
  }

  // Test 3: Promise with then
  try {
    var p3 = Promise.resolve(10);
    var result = await p3.then(function(x) { return x + 5; });
    if (result !== 15) {
      console.log('FAIL: Promise.then value incorrect');
      failures = failures + 1;
    } else {
      console.log('PASS: Promise.then');
    }
  } catch (e) {
    console.log('FAIL: Promise.then - Exception');
    failures = failures + 1;
  }

  // Test 4: Promise chaining
  try {
    var result2 = await Promise.resolve(1)
      .then(function(x) { return x + 1; })
      .then(function(x) { return x + 1; });
    if (result2 !== 3) {
      console.log('FAIL: Promise chaining value incorrect');
      failures = failures + 1;
    } else {
      console.log('PASS: Promise chaining');
    }
  } catch (e) {
    console.log('FAIL: Promise chaining - Exception');
    failures = failures + 1;
  }

  // Test 5: Promise constructor with executor
  try {
    var p5 = new Promise(function(resolve, reject) {
      resolve(100);
    });
    var val5 = await p5;
    if (val5 !== 100) {
      console.log('FAIL: Promise constructor value incorrect');
      failures = failures + 1;
    } else {
      console.log('PASS: Promise constructor');
    }
  } catch (e) {
    console.log('FAIL: Promise constructor - Exception');
    failures = failures + 1;
  }

  // Test 6: Promise.resolve with string
  try {
    var p6 = Promise.resolve('hello');
    var val6 = await p6;
    if (val6.length !== 5) {
      console.log('FAIL: Promise.resolve string incorrect');
      failures = failures + 1;
    } else {
      console.log('PASS: Promise.resolve with string');
    }
  } catch (e) {
    console.log('FAIL: Promise.resolve string - Exception');
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
