// Process Extended API Tests (platform, arch, version, pid, etc.)

function user_main(): number {
  let failures = 0;
  console.log('=== Process Extended API Tests ===\n');

  // Test 1: process.platform returns non-empty string
  try {
    const platform = process.platform;
    if (typeof platform !== 'string' || platform.length === 0) {
      console.log('FAIL: process.platform not a non-empty string');
      failures++;
    } else {
      console.log('PASS: process.platform');
    }
  } catch (e) {
    console.log('FAIL: process.platform - Exception');
    failures++;
  }

  // Test 2: process.arch returns non-empty string
  try {
    const arch = process.arch;
    if (typeof arch !== 'string' || arch.length === 0) {
      console.log('FAIL: process.arch not a non-empty string');
      failures++;
    } else {
      console.log('PASS: process.arch');
    }
  } catch (e) {
    console.log('FAIL: process.arch - Exception');
    failures++;
  }

  // Test 3: process.pid returns number
  try {
    const pid = process.pid;
    if (typeof pid !== 'number') {
      console.log('FAIL: process.pid not a number');
      failures++;
    } else {
      console.log('PASS: process.pid');
    }
  } catch (e) {
    console.log('FAIL: process.pid - Exception');
    failures++;
  }

  // Test 4: process.ppid returns number
  try {
    const ppid = process.ppid;
    if (typeof ppid !== 'number') {
      console.log('FAIL: process.ppid not a number');
      failures++;
    } else {
      console.log('PASS: process.ppid');
    }
  } catch (e) {
    console.log('FAIL: process.ppid - Exception');
    failures++;
  }

  // Test 5: process.version returns non-empty string
  try {
    const version = process.version;
    if (typeof version !== 'string' || version.length === 0) {
      console.log('FAIL: process.version not a non-empty string');
      failures++;
    } else {
      console.log('PASS: process.version');
    }
  } catch (e) {
    console.log('FAIL: process.version - Exception');
    failures++;
  }

  // Test 6: process.execPath returns non-empty string
  try {
    const execPath = process.execPath;
    if (typeof execPath !== 'string' || execPath.length === 0) {
      console.log('FAIL: process.execPath not a non-empty string');
      failures++;
    } else {
      console.log('PASS: process.execPath');
    }
  } catch (e) {
    console.log('FAIL: process.execPath - Exception');
    failures++;
  }

  // Test 7: process.hrtime() returns array
  try {
    const hrStart = process.hrtime();
    if (typeof hrStart.length !== 'number') {
      console.log('FAIL: process.hrtime() did not return array');
      failures++;
    } else {
      console.log('PASS: process.hrtime() returns array');
    }
  } catch (e) {
    console.log('FAIL: process.hrtime() - Exception');
    failures++;
  }

  // Test 8: process.hrtime() with previous time
  try {
    const hrStart = process.hrtime();
    const hrEnd = process.hrtime(hrStart);
    if (typeof hrEnd.length !== 'number') {
      console.log('FAIL: process.hrtime(prev) did not return array');
      failures++;
    } else {
      console.log('PASS: process.hrtime(prev)');
    }
  } catch (e) {
    console.log('FAIL: process.hrtime(prev) - Exception');
    failures++;
  }

  // Test 9: process.uptime() returns number
  try {
    const uptime = process.uptime();
    if (typeof uptime !== 'number') {
      console.log('FAIL: process.uptime() not a number');
      failures++;
    } else {
      console.log('PASS: process.uptime()');
    }
  } catch (e) {
    console.log('FAIL: process.uptime() - Exception');
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
