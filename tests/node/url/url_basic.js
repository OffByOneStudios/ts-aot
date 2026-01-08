// URL Module Basic Tests - JavaScript version

function user_main() {
  var failures = 0;
  console.log('=== URL Module Tests (JS) ===\n');

  // Test 1: URL parsing
  try {
    var url = new URL("https://example.com:8080/path/to/resource?query=1#hash");
    console.log('PASS: URL constructor');
  } catch (e) {
    console.log('FAIL: URL constructor - Exception');
    failures = failures + 1;
  }

  // Test 2: URL protocol
  try {
    var url2 = new URL("https://example.com");
    if (typeof url2.protocol !== 'string') {
      console.log('FAIL: URL.protocol should be string');
      failures = failures + 1;
    } else {
      console.log('PASS: URL.protocol');
    }
  } catch (e) {
    console.log('FAIL: URL.protocol - Exception');
    failures = failures + 1;
  }

  // Test 3: URL host
  try {
    var url3 = new URL("https://example.com:8080");
    if (typeof url3.host !== 'string') {
      console.log('FAIL: URL.host should be string');
      failures = failures + 1;
    } else {
      console.log('PASS: URL.host');
    }
  } catch (e) {
    console.log('FAIL: URL.host - Exception');
    failures = failures + 1;
  }

  // Test 4: URL hostname
  try {
    var url4 = new URL("https://example.com:8080");
    if (typeof url4.hostname !== 'string') {
      console.log('FAIL: URL.hostname should be string');
      failures = failures + 1;
    } else {
      console.log('PASS: URL.hostname');
    }
  } catch (e) {
    console.log('FAIL: URL.hostname - Exception');
    failures = failures + 1;
  }

  // Test 5: URL pathname
  try {
    var url5 = new URL("https://example.com/path/to/resource");
    if (typeof url5.pathname !== 'string') {
      console.log('FAIL: URL.pathname should be string');
      failures = failures + 1;
    } else {
      console.log('PASS: URL.pathname');
    }
  } catch (e) {
    console.log('FAIL: URL.pathname - Exception');
    failures = failures + 1;
  }

  // Test 6: URL search
  try {
    var url6 = new URL("https://example.com?query=1");
    if (typeof url6.search !== 'string') {
      console.log('FAIL: URL.search should be string');
      failures = failures + 1;
    } else {
      console.log('PASS: URL.search');
    }
  } catch (e) {
    console.log('FAIL: URL.search - Exception');
    failures = failures + 1;
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
