// URL Module Basic Tests

function user_main(): number {
  let failures = 0;
  console.log('=== URL Module Tests ===\n');

  // Test 1: URL parsing
  try {
    const url = new URL("https://example.com:8080/path/to/resource?query=1#hash");
    console.log('PASS: URL constructor');
    console.log('  href:', url.href);
  } catch (e) {
    console.log('FAIL: URL constructor - Exception');
    failures++;
  }

  // Test 2: URL protocol
  try {
    const url = new URL("https://example.com");
    if (typeof url.protocol !== 'string') {
      console.log('FAIL: URL.protocol should be string');
      failures++;
    } else {
      console.log('PASS: URL.protocol');
      console.log('  protocol:', url.protocol);
    }
  } catch (e) {
    console.log('FAIL: URL.protocol - Exception');
    failures++;
  }

  // Test 3: URL host
  try {
    const url = new URL("https://example.com:8080");
    if (typeof url.host !== 'string') {
      console.log('FAIL: URL.host should be string');
      failures++;
    } else {
      console.log('PASS: URL.host');
      console.log('  host:', url.host);
    }
  } catch (e) {
    console.log('FAIL: URL.host - Exception');
    failures++;
  }

  // Test 4: URL hostname
  try {
    const url = new URL("https://example.com:8080");
    if (typeof url.hostname !== 'string') {
      console.log('FAIL: URL.hostname should be string');
      failures++;
    } else {
      console.log('PASS: URL.hostname');
      console.log('  hostname:', url.hostname);
    }
  } catch (e) {
    console.log('FAIL: URL.hostname - Exception');
    failures++;
  }

  // Test 5: URL port
  try {
    const url = new URL("https://example.com:8080");
    if (typeof url.port !== 'string') {
      console.log('FAIL: URL.port should be string');
      failures++;
    } else {
      console.log('PASS: URL.port');
      console.log('  port:', url.port);
    }
  } catch (e) {
    console.log('FAIL: URL.port - Exception');
    failures++;
  }

  // Test 6: URL pathname
  try {
    const url = new URL("https://example.com/path/to/resource");
    if (typeof url.pathname !== 'string') {
      console.log('FAIL: URL.pathname should be string');
      failures++;
    } else {
      console.log('PASS: URL.pathname');
      console.log('  pathname:', url.pathname);
    }
  } catch (e) {
    console.log('FAIL: URL.pathname - Exception');
    failures++;
  }

  // Test 7: URL search
  try {
    const url = new URL("https://example.com?query=1");
    if (typeof url.search !== 'string') {
      console.log('FAIL: URL.search should be string');
      failures++;
    } else {
      console.log('PASS: URL.search');
      console.log('  search:', url.search);
    }
  } catch (e) {
    console.log('FAIL: URL.search - Exception');
    failures++;
  }

  // Test 8: URL hash
  try {
    const url = new URL("https://example.com#hash");
    if (typeof url.hash !== 'string') {
      console.log('FAIL: URL.hash should be string');
      failures++;
    } else {
      console.log('PASS: URL.hash');
      console.log('  hash:', url.hash);
    }
  } catch (e) {
    console.log('FAIL: URL.hash - Exception');
    failures++;
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
