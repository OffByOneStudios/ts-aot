// Buffer Encoding Tests - Milestone 101.2

let passed = 0;
let failed = 0;

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
        passed++;
    } else {
        console.log("FAIL: " + name);
        failed++;
    }
}

// Test 1: Hex encoding
const buf1 = Buffer.from("Hello");
const hex1 = buf1.toString("hex");
test("toString hex encodes 'Hello' to '48656c6c6f'", hex1 === "48656c6c6f");

// Test 2: Hex with bytes
const buf2 = Buffer.alloc(3);
buf2[0] = 0;
buf2[1] = 255;
buf2[2] = 128;
const hex2 = buf2.toString("hex");
test("toString hex encodes [0, 255, 128] to '00ff80'", hex2 === "00ff80");

// Test 3: Buffer.from with hex encoding
const buf3 = Buffer.from("48656c6c6f", "hex");
test("Buffer.from hex decodes to correct length", buf3.length === 5);
test("Buffer.from hex decodes to correct bytes", buf3[0] === 72 && buf3[4] === 111);
const str3 = buf3.toString();
test("Buffer.from hex round-trips correctly", str3 === "Hello");

// Test 4: Base64 encoding
const buf4 = Buffer.from("Hello");
const b64_1 = buf4.toString("base64");
test("toString base64 encodes 'Hello' to 'SGVsbG8='", b64_1 === "SGVsbG8=");

// Test 5: Base64 decoding
const buf5 = Buffer.from("SGVsbG8=", "base64");
test("Buffer.from base64 decodes to correct length", buf5.length === 5);
const str5 = buf5.toString();
test("Buffer.from base64 round-trips correctly", str5 === "Hello");

// Test 6: Base64URL encoding (no padding, URL-safe chars)
const buf6 = Buffer.from("Hello");
const b64url = buf6.toString("base64url");
test("toString base64url encodes 'Hello' (no padding)", b64url === "SGVsbG8");

// Test 7: Various base64 cases
const buf7 = Buffer.from("a");
const b64_a = buf7.toString("base64");
test("toString base64 for 'a' is 'YQ=='", b64_a === "YQ==");

const buf8 = Buffer.from("ab");
const b64_ab = buf8.toString("base64");
test("toString base64 for 'ab' is 'YWI='", b64_ab === "YWI=");

const buf9 = Buffer.from("abc");
const b64_abc = buf9.toString("base64");
test("toString base64 for 'abc' is 'YWJj'", b64_abc === "YWJj");

// Test 8: Base64 with special chars (+/)
const buf10 = Buffer.alloc(2);
buf10[0] = 251; // Will produce + in base64
buf10[1] = 239;
const b64_special = buf10.toString("base64");
console.log("Base64 of [251, 239]: " + b64_special);

// Test 9: UTF-8 default encoding still works
const buf11 = Buffer.from("world");
const str11 = buf11.toString("utf8");
test("toString utf8 still works", str11 === "world");

// Summary
console.log("");
console.log("=== Buffer Encoding Tests Complete ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);

if (failed === 0) {
    console.log("All tests passed!");
}
