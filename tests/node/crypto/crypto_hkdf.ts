// Test crypto.hkdfSync() - HKDF key derivation (RFC 5869)
import * as crypto from 'crypto';

function user_main(): number {
    console.log("=== Crypto HKDF Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Basic HKDF derivation with SHA-256
    console.log("\n1. Basic hkdfSync with SHA-256:");
    try {
        const ikm = Buffer.from("input keying material");
        const salt = Buffer.from("salt value");
        const info = Buffer.from("context info");
        const keylen = 32;  // 256 bits

        const derivedKey = crypto.hkdfSync('sha256', ikm, salt, info, keylen);
        if (derivedKey && derivedKey.length === keylen) {
            console.log("  Derived key length: " + derivedKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (wrong length: " + (derivedKey ? derivedKey.length : "null") + ")");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 2: HKDF with SHA-512
    console.log("\n2. hkdfSync with SHA-512:");
    try {
        const ikm = Buffer.from("secret key material");
        const salt = Buffer.from("random salt");
        const info = Buffer.from("application specific info");
        const keylen = 64;  // 512 bits

        const derivedKey = crypto.hkdfSync('sha512', ikm, salt, info, keylen);
        if (derivedKey && derivedKey.length === keylen) {
            console.log("  Derived key length: " + derivedKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (wrong length)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 3: HKDF with empty salt (uses zero-length salt)
    console.log("\n3. hkdfSync with empty salt:");
    try {
        const ikm = Buffer.from("some input key");
        const salt = Buffer.alloc(0);  // Empty salt
        const info = Buffer.from("info");
        const keylen = 16;

        const derivedKey = crypto.hkdfSync('sha256', ikm, salt, info, keylen);
        if (derivedKey && derivedKey.length === keylen) {
            console.log("  Derived key length: " + derivedKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (wrong length)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 4: HKDF with empty info
    console.log("\n4. hkdfSync with empty info:");
    try {
        const ikm = Buffer.from("master secret");
        const salt = Buffer.from("my salt");
        const info = Buffer.alloc(0);  // Empty info
        const keylen = 32;

        const derivedKey = crypto.hkdfSync('sha256', ikm, salt, info, keylen);
        if (derivedKey && derivedKey.length === keylen) {
            console.log("  Derived key length: " + derivedKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (wrong length)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 5: Consistent output for same inputs
    console.log("\n5. Consistent output for same inputs:");
    try {
        const ikm = Buffer.from("test key");
        const salt = Buffer.from("test salt");
        const info = Buffer.from("test info");
        const keylen = 32;

        const key1 = crypto.hkdfSync('sha256', ikm, salt, info, keylen);
        const key2 = crypto.hkdfSync('sha256', ikm, salt, info, keylen);

        let match = true;
        if (key1.length !== key2.length) {
            match = false;
        } else {
            for (let i = 0; i < key1.length; i++) {
                if (key1[i] !== key2[i]) {
                    match = false;
                    break;
                }
            }
        }

        if (match) {
            console.log("  Keys match");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (keys don't match)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 6: Different inputs produce different keys
    console.log("\n6. Different inputs produce different keys:");
    try {
        const salt = Buffer.from("salt");
        const info = Buffer.from("info");
        const keylen = 32;

        const key1 = crypto.hkdfSync('sha256', Buffer.from("key1"), salt, info, keylen);
        const key2 = crypto.hkdfSync('sha256', Buffer.from("key2"), salt, info, keylen);

        let same = true;
        for (let i = 0; i < key1.length; i++) {
            if (key1[i] !== key2[i]) {
                same = false;
                break;
            }
        }

        if (!same) {
            console.log("  Keys are different (as expected)");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (keys should be different)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All HKDF tests passed!");
    }

    return failed;
}
