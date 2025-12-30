// Buffer TypedArray-like properties test - Milestone 101.3

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

// Test 1: buffer.byteLength
const buf1 = Buffer.alloc(10);
test("buffer.byteLength equals length", buf1.byteLength === 10);

// Test 2: buffer.byteOffset is always 0 for Buffer
const buf2 = Buffer.alloc(20);
test("buffer.byteOffset is 0", buf2.byteOffset === 0);

// Test 3: buffer.buffer returns the ArrayBuffer (same as buffer for Node.js)
const buf3 = Buffer.alloc(5);
buf3[0] = 42;
const arrBuf = buf3.buffer;
test("buffer.buffer returns truthy value", arrBuf !== null);
test("buffer.buffer[0] equals original", arrBuf[0] === 42);

// Test 4: Various sizes work correctly
const buf4 = Buffer.alloc(0);
test("buffer.byteLength for empty buffer is 0", buf4.byteLength === 0);

const buf5 = Buffer.alloc(1000);
test("buffer.byteLength for large buffer", buf5.byteLength === 1000);

// Summary
console.log("");
console.log("=== Buffer TypedArray-like Tests Complete ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);

if (failed === 0) {
    console.log("All tests passed!");
}
