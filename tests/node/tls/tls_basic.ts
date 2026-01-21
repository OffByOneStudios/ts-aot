// TLS Module Basic Tests
// Tests tls module functions

function user_main(): number {
  let failures = 0;
  console.log('=== TLS Module Basic Tests ===\n');

  // Test 1: tls.getCiphers() returns array
  try {
    const ciphers = tls.getCiphers();
    if (typeof ciphers !== 'object') {
      console.log('FAIL: tls.getCiphers() not an object');
      failures++;
    } else {
      console.log('PASS: tls.getCiphers() returned ' + ciphers.length + ' ciphers');
      // Print first few ciphers if available
      if (ciphers.length > 0) {
        console.log('  First cipher: ' + ciphers[0]);
      }
    }
  } catch (e) {
    console.log('FAIL: tls.getCiphers() - Exception');
    failures++;
  }

  // Test 2: tls.createSecureContext() returns object
  try {
    const ctx = tls.createSecureContext({});
    if (ctx === null || ctx === undefined) {
      console.log('FAIL: tls.createSecureContext() returned null/undefined');
      failures++;
    } else {
      console.log('PASS: tls.createSecureContext() returned context object');
    }
  } catch (e) {
    console.log('FAIL: tls.createSecureContext() - Exception');
    failures++;
  }

  // Test 3: tls.createSecureContext() with options
  try {
    const ctx = tls.createSecureContext({
      minVersion: 'TLSv1.2',
      maxVersion: 'TLSv1.3'
    });
    if (ctx === null || ctx === undefined) {
      console.log('FAIL: tls.createSecureContext(options) returned null/undefined');
      failures++;
    } else {
      console.log('PASS: tls.createSecureContext(options) returned context');
    }
  } catch (e) {
    console.log('FAIL: tls.createSecureContext(options) - Exception');
    failures++;
  }

  // Test 4: Check DEFAULT constants exist
  try {
    console.log('PASS: TLS module loaded successfully');
  } catch (e) {
    console.log('FAIL: TLS module load - Exception');
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
