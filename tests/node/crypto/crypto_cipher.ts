// Test crypto.createCipheriv and crypto.createDecipheriv
import * as crypto from 'crypto';

function user_main(): number {
    console.log("=== Crypto Cipher/Decipher Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: AES-256-CBC encryption and decryption
    console.log("\n1. AES-256-CBC encryption/decryption:");
    const key256 = crypto.randomBytes(32);  // 256 bits for AES-256
    console.log("  Key generated, length: " + key256.length);
    const iv = crypto.randomBytes(16);      // 16 bytes IV for CBC mode
    console.log("  IV generated, length: " + iv.length);
    const plaintext = "Hello, World! This is a secret message.";

    console.log("  Creating cipher...");
    const cipher = crypto.createCipheriv('aes-256-cbc', key256, iv);
    console.log("  Cipher created");
    console.log("  Calling cipher.update...");
    const encrypted1 = cipher.update(plaintext);
    console.log("  Update result: " + (encrypted1 ? "ok" : "null"));
    console.log("  Calling cipher.final...");
    const encrypted2 = cipher.final();
    console.log("  Final result: " + (encrypted2 ? "ok" : "null"));

    // Concatenate the encrypted buffers
    const encrypted = Buffer.concat([encrypted1, encrypted2]);
    console.log("  Encrypted length: " + encrypted.length);

    const decipher = crypto.createDecipheriv('aes-256-cbc', key256, iv);
    const decrypted1 = decipher.update(encrypted);
    const decrypted2 = decipher.final();

    const decrypted = Buffer.concat([decrypted1, decrypted2]);
    const decryptedText = decrypted.toString('utf8');
    console.log("  Decrypted: " + decryptedText);

    if (decryptedText === plaintext) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected: " + plaintext + ")");
        failed++;
    }

    // Test 2: AES-128-CBC with different key size
    console.log("\n2. AES-128-CBC encryption/decryption:");
    const key128 = crypto.randomBytes(16);  // 128 bits for AES-128
    const iv2 = crypto.randomBytes(16);
    const message2 = "Short message";

    const cipher2 = crypto.createCipheriv('aes-128-cbc', key128, iv2);
    const enc2a = cipher2.update(message2);
    const enc2b = cipher2.final();
    const enc2 = Buffer.concat([enc2a, enc2b]);

    const decipher2 = crypto.createDecipheriv('aes-128-cbc', key128, iv2);
    const dec2a = decipher2.update(enc2);
    const dec2b = decipher2.final();
    const dec2 = Buffer.concat([dec2a, dec2b]);
    const result2 = dec2.toString('utf8');
    console.log("  Decrypted: " + result2);

    if (result2 === message2) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 3: crypto.getCiphers() includes aes ciphers
    console.log("\n3. getCiphers() includes AES ciphers:");
    const ciphers = crypto.getCiphers();
    let hasAes256 = false;
    let hasAes128 = false;
    for (let i = 0; i < ciphers.length; i++) {
        if (ciphers[i] === 'aes-256-cbc') hasAes256 = true;
        if (ciphers[i] === 'aes-128-cbc') hasAes128 = true;
    }
    console.log("  Has aes-256-cbc: " + hasAes256);
    console.log("  Has aes-128-cbc: " + hasAes128);
    if (hasAes256 && hasAes128) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All crypto cipher tests passed!");
    }

    return failed;
}
