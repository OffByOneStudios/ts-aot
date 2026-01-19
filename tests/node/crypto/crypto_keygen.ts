// Test crypto key generation functions
import * as crypto from 'crypto';

function user_main(): number {
    console.log("=== Crypto Key Generation Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Generate RSA key pair synchronously
    console.log("\n1. generateKeyPairSync('rsa'):");
    try {
        const keyPair = crypto.generateKeyPairSync('rsa', {
            modulusLength: 2048
        });
        const hasPublic = keyPair.publicKey && keyPair.publicKey.length > 0;
        const hasPrivate = keyPair.privateKey && keyPair.privateKey.length > 0;
        const pubStartsCorrectly = keyPair.publicKey.startsWith('-----BEGIN PUBLIC KEY-----');
        const privStartsCorrectly = keyPair.privateKey.startsWith('-----BEGIN RSA PRIVATE KEY-----') ||
                                    keyPair.privateKey.startsWith('-----BEGIN PRIVATE KEY-----');
        if (hasPublic && hasPrivate && pubStartsCorrectly && privStartsCorrectly) {
            console.log("  Public key length: " + keyPair.publicKey.length);
            console.log("  Private key length: " + keyPair.privateKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (invalid key format)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 2: Generate EC key pair (P-256)
    console.log("\n2. generateKeyPairSync('ec') with P-256:");
    try {
        const keyPair = crypto.generateKeyPairSync('ec', {
            namedCurve: 'P-256'
        });
        const hasPublic = keyPair.publicKey && keyPair.publicKey.length > 0;
        const hasPrivate = keyPair.privateKey && keyPair.privateKey.length > 0;
        if (hasPublic && hasPrivate) {
            console.log("  Public key length: " + keyPair.publicKey.length);
            console.log("  Private key length: " + keyPair.privateKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (missing keys)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 3: Generate EC key pair (P-384)
    console.log("\n3. generateKeyPairSync('ec') with P-384:");
    try {
        const keyPair = crypto.generateKeyPairSync('ec', {
            namedCurve: 'P-384'
        });
        const hasPublic = keyPair.publicKey && keyPair.publicKey.length > 0;
        const hasPrivate = keyPair.privateKey && keyPair.privateKey.length > 0;
        if (hasPublic && hasPrivate) {
            console.log("  Public key length: " + keyPair.publicKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (missing keys)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 4: Generate Ed25519 key pair
    console.log("\n4. generateKeyPairSync('ed25519'):");
    try {
        const keyPair = crypto.generateKeyPairSync('ed25519', {});
        const hasPublic = keyPair.publicKey && keyPair.publicKey.length > 0;
        const hasPrivate = keyPair.privateKey && keyPair.privateKey.length > 0;
        if (hasPublic && hasPrivate) {
            console.log("  Public key length: " + keyPair.publicKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (missing keys)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 5: Generate X25519 key pair
    console.log("\n5. generateKeyPairSync('x25519'):");
    try {
        const keyPair = crypto.generateKeyPairSync('x25519', {});
        const hasPublic = keyPair.publicKey && keyPair.publicKey.length > 0;
        const hasPrivate = keyPair.privateKey && keyPair.privateKey.length > 0;
        if (hasPublic && hasPrivate) {
            console.log("  Public key length: " + keyPair.publicKey.length);
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (missing keys)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 6: Generate symmetric key (AES-256)
    console.log("\n6. generateKeySync('aes', 256):");
    try {
        const key = crypto.generateKeySync('aes', { length: 256 });
        if (key && key.length === 32) {  // 256 bits = 32 bytes
            console.log("  Key length: " + key.length + " bytes");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (wrong length: " + (key ? key.length : 'null') + ")");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 7: Generate symmetric key (HMAC)
    console.log("\n7. generateKeySync('hmac', 256):");
    try {
        const key = crypto.generateKeySync('hmac', { length: 256 });
        if (key && key.length === 32) {  // 256 bits = 32 bytes
            console.log("  Key length: " + key.length + " bytes");
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

    // Test 8: createPrivateKey (from PEM string)
    console.log("\n8. createPrivateKey from PEM:");
    try {
        // First generate a key pair
        const keyPair = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
        // Then try to create a KeyObject from it
        const keyObj = crypto.createPrivateKey(keyPair.privateKey);
        if (keyObj) {
            console.log("  KeyObject created successfully");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (null returned)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 9: createPublicKey (from PEM string)
    console.log("\n9. createPublicKey from PEM:");
    try {
        // First generate a key pair
        const keyPair = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
        // Then try to create a KeyObject from it
        const keyObj = crypto.createPublicKey(keyPair.publicKey);
        if (keyObj) {
            console.log("  KeyObject created successfully");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (null returned)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 10: createSecretKey (from Buffer)
    console.log("\n10. createSecretKey from Buffer:");
    try {
        const key = crypto.randomBytes(32);
        const keyObj = crypto.createSecretKey(key);
        if (keyObj) {
            console.log("  SecretKey created successfully");
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (null returned)");
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
        console.log("All crypto key generation tests passed!");
    }

    return failed;
}
