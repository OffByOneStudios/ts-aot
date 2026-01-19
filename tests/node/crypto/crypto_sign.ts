// Test crypto.createSign and crypto.createVerify for digital signatures
import * as crypto from 'crypto';

function user_main(): number {
    console.log("=== Crypto Sign/Verify Tests ===");
    let passed = 0;
    let failed = 0;

    // Test key pair (RSA 2048-bit generated with: openssl genrsa 2048)
    const privateKeyPem = `-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEAseDZE1diR7Rh3HGg7NgiSFfb63aUxkXOrJ7OFUKzjd0k3R4y
hTrbdVTNqSZGKx3kH0JQE5bDlZBSA7jX3eGxwcGcaWpKYDOj5aZ5ySNqBlyimO5O
UXiuHGUFyTSRHdeiVV6Vk7K1kGZF5Wtxgl81AT/Zzjd8t6JNTkc2Xmqv8ZO21f3+
MTEivlVHplsQSCz/AjX01Y+u6tn3K7ji7Tbo3FnNUe0p7/6nChuoDXVLREN0KCkj
8HM8WuF0m0YiUeQ+oO/LDz7INfJwKSSx2id0SnfyIlCT3emDABjhZ3Y0UQTvrN5V
H2evAP8+2E5HHxd9WoigY1mLgQTaru1Qbe500wIDAQABAoIBAQCDisTdMcUDQ9kO
3ZTlMZyApUCEK4gv7BW/wBykylPgOEb3oko4DmOWrCT+zsgbQJqfpzlykw6GP1j3
Wa2Bb1M19wjFSA5CgE3XGbp8TU0t8TlpIOoYA8e2Rdr2hYI/cGSi/zRcKk9svsU3
uLITeHbJOr/GaXA4mLTUi4MgZYNBgQCZ8mY8cScvvmK32vmn2aJllm/Lk1wxg5kd
QSx9rFGui1u00lh4B/18vQLa8+1l5n9D1BpmxkUrg7B401CMy9GZjS2lSpHd3daz
VC+HXKLVTNc6Hfi8HLklVItz0Qnsz55pAmuwpcJTYv2BYNljedRVZMce3ouASSlZ
JcZ7StCBAoGBANWicWnviR7Hal0mZv17j8FeGM2XzAN1OtPI0Te18/TSVTkbnHfg
YUaZceG0KTcMfRtCCpmTK2YOSyVytvh4i222tWPElxsy8nSfgiMRQ8y61KjpKGR3
jsfQUGqlcDDDDtGchnoifcASwXUvxxx2W/lV4wC7A5oAjAbhB5mJzQJBAoGBANUn
LEt+2nemUx1jC7a0qHgn22+V8Etw49GhO6KqM9AOyZd7GsNdBb0rfZorQ6hId4wO
D6IM3IZlpCStM2OioeHTlFNzB9uehNeAeJYhGxWi62Eleti0CA8Httal+7Gjue/E
hMVvtWsPiH0XZ2wM8cvT9e+scm7y5V3+nTKT8MoTAoGANElVyL+/p7DW74V+n3um
a4VEPM1yOUZv53W2/xOhacIw6ZFAcnaQWF8l6D/X9okv9YPsZDoI3SmSas/wyE94
kJmvO4PaF+YYQULo7vxCw9DWS6EFKdG5OF5b0D49fzG+Zr8QisP2UxREFRJkgSrW
x+elb4BWGVMY8nYRDhsT+gECgYEAggWAPpkl2MCriIyHc67l6U2ezIVw7APz5Ebu
4r3iFzM+A1pDrBJNUuR5nJZxkfCKg/N708T2rEDKDNleNJPbHa77lp/fljcvH2mt
pR6Sr/MOk9bSBehj1g9Fl8/uJaES5dBBkVIgHyt9fZjOLJoE0On95nKR513hTiHn
cBfQVOMCgYBizig3JDTwVvdy3xF1lafdSRert/35jTEBej6rksucjrz4n16zyFgi
+/4yeOVKnlSvZ/6iIMjN3t0mOOvrpCc2HRg0pohCNq7Vd1iWflSJfMXUrNWEH7TZ
04N4sTl3hHIG4Wwx6hQDb/5bnB7vxow/zq7cHEYIoiRjJ3Vd/IdV2g==
-----END RSA PRIVATE KEY-----`;

    const publicKeyPem = `-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAseDZE1diR7Rh3HGg7Ngi
SFfb63aUxkXOrJ7OFUKzjd0k3R4yhTrbdVTNqSZGKx3kH0JQE5bDlZBSA7jX3eGx
wcGcaWpKYDOj5aZ5ySNqBlyimO5OUXiuHGUFyTSRHdeiVV6Vk7K1kGZF5Wtxgl81
AT/Zzjd8t6JNTkc2Xmqv8ZO21f3+MTEivlVHplsQSCz/AjX01Y+u6tn3K7ji7Tbo
3FnNUe0p7/6nChuoDXVLREN0KCkj8HM8WuF0m0YiUeQ+oO/LDz7INfJwKSSx2id0
SnfyIlCT3emDABjhZ3Y0UQTvrN5VH2evAP8+2E5HHxd9WoigY1mLgQTaru1Qbe50
0wIDAQAB
-----END PUBLIC KEY-----`;

    const testMessage = "Hello, this is a test message for signing!";

    // Test 1: Create Sign object and sign data
    console.log("\n1. Create Sign and sign data:");
    try {
        const sign = crypto.createSign('SHA256');
        sign.update(testMessage);
        const signature = sign.sign(privateKeyPem);
        console.log("  Signature length: " + signature.length);
        if (signature.length > 0) {
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (empty signature)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 2: Create Verify object and verify signature
    console.log("\n2. Create Verify and verify signature:");
    try {
        // First sign
        const sign = crypto.createSign('SHA256');
        sign.update(testMessage);
        const signature = sign.sign(privateKeyPem);

        // Then verify
        const verify = crypto.createVerify('SHA256');
        verify.update(testMessage);
        const isValid = verify.verify(publicKeyPem, signature);
        console.log("  Verification result: " + isValid);
        if (isValid) {
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (verification failed)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 3: Verify should fail with wrong message
    console.log("\n3. Verify fails with wrong message:");
    try {
        // Sign original message
        const sign = crypto.createSign('SHA256');
        sign.update(testMessage);
        const signature = sign.sign(privateKeyPem);

        // Verify with different message
        const verify = crypto.createVerify('SHA256');
        verify.update("Different message");
        const isValid = verify.verify(publicKeyPem, signature);
        console.log("  Verification result: " + isValid);
        if (!isValid) {
            console.log("  PASS (correctly rejected)");
            passed++;
        } else {
            console.log("  FAIL (should have rejected)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 4: One-shot sign function
    console.log("\n4. One-shot crypto.sign():");
    try {
        const data = Buffer.from(testMessage);
        const signature = crypto.sign('SHA256', data, privateKeyPem);
        console.log("  Signature length: " + signature.length);
        if (signature.length > 0) {
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (empty signature)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 5: One-shot verify function
    console.log("\n5. One-shot crypto.verify():");
    try {
        const data = Buffer.from(testMessage);
        const signature = crypto.sign('SHA256', data, privateKeyPem);
        const isValid = crypto.verify('SHA256', data, publicKeyPem, signature);
        console.log("  Verification result: " + isValid);
        if (isValid) {
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (verification failed)");
            failed++;
        }
    } catch (e) {
        console.log("  FAIL (exception)");
        failed++;
    }

    // Test 6: Multiple updates before signing
    console.log("\n6. Multiple updates before signing:");
    try {
        const sign = crypto.createSign('SHA256');
        sign.update("Part 1 ");
        sign.update("Part 2 ");
        sign.update("Part 3");
        const signature = sign.sign(privateKeyPem);

        const verify = crypto.createVerify('SHA256');
        verify.update("Part 1 ");
        verify.update("Part 2 ");
        verify.update("Part 3");
        const isValid = verify.verify(publicKeyPem, signature);
        console.log("  Verification result: " + isValid);
        if (isValid) {
            console.log("  PASS");
            passed++;
        } else {
            console.log("  FAIL (verification failed)");
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
        console.log("All crypto sign/verify tests passed!");
    }

    return failed;
}
