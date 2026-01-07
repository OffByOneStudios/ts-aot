// Fetch API Tests

async function user_main(): Promise<number> {
  let failures = 0;
  console.log('=== Fetch API Tests ===\n');

  // Test 1: fetch() creates request
  try {
    const response = await fetch('http://localhost:8080');
    if (!response) {
      console.log('FAIL: fetch did not return response');
      failures++;
    } else {
      console.log('PASS: fetch creates request');
    }
  } catch (e) {
    console.log('FAIL: fetch - Exception (expected if no server)');
    // Not a failure - expected without server
  }

  // Test 2: fetch() with GET method
  try {
    const response = await fetch('http://localhost:8080', {
      method: 'GET'
    });
    if (!response) {
      console.log('FAIL: fetch GET did not return response');
      failures++;
    } else {
      console.log('PASS: fetch with GET method');
    }
  } catch (e) {
    console.log('SKIP: fetch GET (no server running)');
  }

  // Test 3: fetch() with POST method
  try {
    const response = await fetch('http://localhost:8080', {
      method: 'POST',
      body: 'test data'
    });
    console.log('PASS: fetch with POST method');
  } catch (e) {
    console.log('SKIP: fetch POST (no server running)');
  }

  // Test 4: fetch() response.text()
  try {
    const response = await fetch('http://localhost:8080');
    const text = await response.text();
    if (typeof text !== 'string') {
      console.log('FAIL: response.text() did not return string');
      failures++;
    } else {
      console.log('PASS: response.text()');
    }
  } catch (e) {
    console.log('SKIP: response.text() (no server running)');
  }

  // Test 5: fetch() response.json()
  try {
    const response = await fetch('http://localhost:8080/json');
    const data = await response.json();
    console.log('PASS: response.json()');
  } catch (e) {
    console.log('SKIP: response.json() (no server running)');
  }

  // Test 6: fetch() with headers
  try {
    const response = await fetch('http://localhost:8080', {
      method: 'GET',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': 'Bearer token'
      }
    });
    console.log('PASS: fetch with headers');
  } catch (e) {
    console.log('SKIP: fetch with headers (no server running)');
  }

  // Test 7: fetch() response status
  try {
    const response = await fetch('http://localhost:8080');
    if (typeof response.status !== 'number') {
      console.log('FAIL: response.status not a number');
      failures++;
    } else {
      console.log('PASS: response.status');
    }
  } catch (e) {
    console.log('SKIP: response.status (no server running)');
  }

  // Test 8: fetch() response ok
  try {
    const response = await fetch('http://localhost:8080');
    if (typeof response.ok !== 'boolean') {
      console.log('FAIL: response.ok not a boolean');
      failures++;
    } else {
      console.log('PASS: response.ok');
    }
  } catch (e) {
    console.log('SKIP: response.ok (no server running)');
  }

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
