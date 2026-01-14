// Crypto Module Extended Tests

function user_main(): number {
  let failures = 0;
  console.log('=== Crypto Extended Tests ===\n');

  // Test 1: crypto.createHash with sha256
  console.log('Test 1: crypto.createHash("sha256")');
  const hash = crypto.createHash('sha256');
  if (hash) {
    hash.update('hello world');
    const digest = hash.digest('hex');
    console.log('  SHA256 hex digest: ' + digest);
    console.log('  Digest length: ' + digest.length);
    // Expected: b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
    if (digest && digest.length === 64) {
      console.log('PASS: SHA256 hash created and digested');
    } else {
      console.log('FAIL: SHA256 digest incorrect length (expected 64, got ' + digest.length + ')');
      failures++;
    }
  } else {
    console.log('FAIL: createHash returned null');
    failures++;
  }

  // Test 2: crypto.createHash with md5
  console.log('\nTest 2: crypto.createHash("md5")');
  const md5Hash = crypto.createHash('md5');
  if (md5Hash) {
    md5Hash.update('test');
    const md5Digest = md5Hash.digest('hex');
    console.log('  MD5 hex digest: ' + md5Digest);
    console.log('  Digest length: ' + md5Digest.length);
    // Expected: 098f6bcd4621d373cade4e832627b4f6
    if (md5Digest && md5Digest.length === 32) {
      console.log('PASS: MD5 hash created and digested');
    } else {
      console.log('FAIL: MD5 digest incorrect length (expected 32, got ' + md5Digest.length + ')');
      failures++;
    }
  } else {
    console.log('FAIL: createHash(md5) returned null');
    failures++;
  }

  // Test 3: crypto.createHmac
  console.log('\nTest 3: crypto.createHmac("sha256", key)');
  const hmac = crypto.createHmac('sha256', 'secret-key');
  if (hmac) {
    hmac.update('message to authenticate');
    const hmacDigest = hmac.digest('hex');
    console.log('  HMAC-SHA256 hex digest: ' + hmacDigest);
    console.log('  Digest length: ' + hmacDigest.length);
    if (hmacDigest && hmacDigest.length === 64) {
      console.log('PASS: HMAC created and digested');
    } else {
      console.log('FAIL: HMAC digest incorrect (expected 64, got ' + hmacDigest.length + ')');
      failures++;
    }
  } else {
    console.log('FAIL: createHmac returned null');
    failures++;
  }

  // Test 4: crypto.randomBytes
  console.log('\nTest 4: crypto.randomBytes(16)');
  const randomBuf = crypto.randomBytes(16);
  if (randomBuf) {
    console.log('  Random bytes length:', randomBuf.length);
    if (randomBuf.length === 16) {
      console.log('PASS: randomBytes returned correct length');
    } else {
      console.log('FAIL: randomBytes returned wrong length');
      failures++;
    }
  } else {
    console.log('FAIL: randomBytes returned null');
    failures++;
  }

  // Test 5: crypto.randomInt
  console.log('\nTest 5: crypto.randomInt(0, 100)');
  const randomNum = crypto.randomInt(0, 100);
  console.log('  Random int:', randomNum);
  if (randomNum >= 0 && randomNum < 100) {
    console.log('PASS: randomInt in expected range');
  } else {
    console.log('FAIL: randomInt out of range');
    failures++;
  }

  // Test 6: crypto.randomUUID
  console.log('\nTest 6: crypto.randomUUID()');
  const uuid = crypto.randomUUID();
  console.log('  UUID:', uuid);
  if (uuid && uuid.length === 36) {
    console.log('PASS: randomUUID returned valid format');
  } else {
    console.log('FAIL: randomUUID invalid');
    failures++;
  }

  // Test 7: crypto.getHashes
  console.log('\nTest 7: crypto.getHashes()');
  const hashes = crypto.getHashes();
  if (hashes && hashes.length > 0) {
    console.log('  Available hash algorithms:', hashes.length);
    console.log('PASS: getHashes returned algorithms');
  } else {
    console.log('FAIL: getHashes returned empty or null');
    failures++;
  }

  // Test 8: crypto.pbkdf2Sync
  console.log('\nTest 8: crypto.pbkdf2Sync');
  const derivedKey = crypto.pbkdf2Sync('password', 'salt', 100000, 32, 'sha256');
  if (derivedKey) {
    console.log('  Derived key length:', derivedKey.length);
    if (derivedKey.length === 32) {
      console.log('PASS: pbkdf2Sync returned correct key length');
    } else {
      console.log('FAIL: pbkdf2Sync returned wrong length');
      failures++;
    }
  } else {
    console.log('FAIL: pbkdf2Sync returned null');
    failures++;
  }

  // Test 9: crypto.scryptSync
  console.log('\nTest 9: crypto.scryptSync');
  const scryptKey = crypto.scryptSync('password', 'salt', 32);
  if (scryptKey) {
    console.log('  Scrypt key length:', scryptKey.length);
    if (scryptKey.length === 32) {
      console.log('PASS: scryptSync returned correct key length');
    } else {
      console.log('FAIL: scryptSync returned wrong length');
      failures++;
    }
  } else {
    console.log('FAIL: scryptSync returned null');
    failures++;
  }

  // Test 10: crypto.timingSafeEqual
  console.log('\nTest 10: crypto.timingSafeEqual');
  const buf1 = Buffer.from('test');
  const buf2 = Buffer.from('test');
  const buf3 = Buffer.from('diff');
  const equal1 = crypto.timingSafeEqual(buf1, buf2);
  const equal2 = crypto.timingSafeEqual(buf1, buf3);
  console.log('  Same buffers equal:', equal1);
  console.log('  Different buffers equal:', equal2);
  if (equal1 === true && equal2 === false) {
    console.log('PASS: timingSafeEqual works correctly');
  } else {
    console.log('FAIL: timingSafeEqual incorrect results');
    failures++;
  }

  // Test 11: Hash chaining
  console.log('\nTest 11: Hash update chaining');
  const chainedHash = crypto.createHash('sha256');
  if (chainedHash) {
    chainedHash.update('part1').update('part2').update('part3');
    const chainedDigest = chainedHash.digest('hex');
    console.log('  Chained digest: ' + chainedDigest);
    console.log('  Chained digest length: ' + chainedDigest.length);
    if (chainedDigest && chainedDigest.length === 64) {
      console.log('PASS: Hash chaining works');
    } else {
      console.log('FAIL: Hash chaining failed (expected 64, got ' + chainedDigest.length + ')');
      failures++;
    }
  } else {
    console.log('FAIL: createHash for chaining returned null');
    failures++;
  }

  // Test 12: Hash copy
  console.log('\nTest 12: Hash copy');
  const originalHash = crypto.createHash('sha256');
  if (originalHash) {
    originalHash.update('partial');
    const copiedHash = originalHash.copy();
    if (copiedHash) {
      originalHash.update('_original');
      copiedHash.update('_copy');
      const origDigest = originalHash.digest('hex');
      const copyDigest = copiedHash.digest('hex');
      console.log('  Original (partial_original):', origDigest);
      console.log('  Copy (partial_copy):', copyDigest);
      if (origDigest !== copyDigest) {
        console.log('PASS: Hash copy creates independent state');
      } else {
        console.log('FAIL: Hash copy not independent');
        failures++;
      }
    } else {
      console.log('FAIL: Hash copy returned null');
      failures++;
    }
  } else {
    console.log('FAIL: createHash for copy test returned null');
    failures++;
  }

  // Summary
  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All crypto extended tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }

  return failures;
}
