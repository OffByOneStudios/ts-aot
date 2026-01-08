// HTTP Client Tests
// Uses fetch API to make real HTTP requests

async function user_main(): Promise<number> {
  let failures = 0;
  console.log('=== HTTP Client Tests ===\n');

  // Test 1: Make a real HTTP GET request to httpbin.org using fetch
  try {
    const response = await fetch('http://httpbin.org/get');
    if (response.ok) {
      console.log('PASS: HTTP GET request succeeded');
    } else {
      console.log('FAIL: HTTP GET request failed');
      failures = failures + 1;
    }
  } catch (e) {
    console.log('FAIL: HTTP request error');
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
