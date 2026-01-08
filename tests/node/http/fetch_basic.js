// Fetch API Tests - JavaScript version
// Uses real endpoint instead of localhost

async function user_main() {
  var failures = 0;
  console.log('=== Fetch API Tests (JS) ===\n');

  // Test 1: fetch() with real endpoint
  try {
    var response = await fetch('http://httpbin.org/get');
    if (response.ok) {
      console.log('PASS: fetch creates request');
    } else {
      console.log('FAIL: fetch request failed');
      failures = failures + 1;
    }
  } catch (e) {
    console.log('FAIL: fetch - Exception');
    failures = failures + 1;
  }

  // Test 2: fetch() response.ok property
  try {
    var response2 = await fetch('http://httpbin.org/get');
    if (typeof response2.ok === 'boolean') {
      console.log('PASS: response.ok is boolean');
    } else {
      console.log('FAIL: response.ok not a boolean');
      failures = failures + 1;
    }
  } catch (e) {
    console.log('FAIL: response.ok - Exception');
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
