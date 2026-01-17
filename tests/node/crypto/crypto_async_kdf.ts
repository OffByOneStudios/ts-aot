// Test async key derivation functions: crypto.pbkdf2() and crypto.scrypt()
import * as crypto from 'crypto';

let testsPassed = 0;
let testsFailed = 0;

function test(name: string, passed: boolean): void {
    if (passed) {
        console.log("PASS: " + name);
        testsPassed++;
    } else {
        console.log("FAIL: " + name);
        testsFailed++;
    }
}

function user_main(): number {
    console.log("=== Testing async crypto.pbkdf2 and crypto.scrypt ===");

    // Test 1: crypto.pbkdf2 with string password and salt
    crypto.pbkdf2("password", "salt", 100000, 32, "sha256", (err: any, derivedKey: Buffer) => {
        test("pbkdf2 callback called", true);
        test("pbkdf2 no error", err === undefined || err === null);
        test("pbkdf2 result is buffer", derivedKey !== undefined && derivedKey !== null);
        if (derivedKey) {
            test("pbkdf2 key length correct", derivedKey.length === 32);
            // Expected hex for pbkdf2("password", "salt", 100000, 32, "sha256")
            const hex = derivedKey.toString("hex");
            console.log("pbkdf2 derived key: " + hex);
            test("pbkdf2 key not empty", hex.length > 0);
        }

        // Test 2: crypto.scrypt with string password and salt
        crypto.scrypt("password", "salt", 32, (err2: any, derivedKey2: Buffer) => {
            test("scrypt callback called", true);
            test("scrypt no error", err2 === undefined || err2 === null);
            test("scrypt result is buffer", derivedKey2 !== undefined && derivedKey2 !== null);
            if (derivedKey2) {
                test("scrypt key length correct", derivedKey2.length === 32);
                const hex2 = derivedKey2.toString("hex");
                console.log("scrypt derived key: " + hex2);
                test("scrypt key not empty", hex2.length > 0);
            }

            // Print final results
            console.log("");
            console.log("=== Test Summary ===");
            console.log("Passed: " + testsPassed);
            console.log("Failed: " + testsFailed);
        });
    });

    return 0;
}
