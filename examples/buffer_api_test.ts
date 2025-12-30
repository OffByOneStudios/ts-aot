// Buffer API Tests - Epic 101

let passed = 0;
let failed = 0;

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("✓ " + name);
        passed++;
    } else {
        console.log("✗ " + name);
        failed++;
    }
}

// Test 1: Buffer.alloc
const buf1 = Buffer.alloc(10);
test("Buffer.alloc creates buffer with correct length", buf1.length === 10);

// Test 2: Buffer.alloc is zero-filled
const allZeros = buf1[0] === 0 && buf1[5] === 0 && buf1[9] === 0;
test("Buffer.alloc is zero-filled", allZeros);

// Test 3: Buffer.allocUnsafe
const buf2 = Buffer.allocUnsafe(5);
test("Buffer.allocUnsafe creates buffer with correct length", buf2.length === 5);

// Test 4: Buffer.from string
const buf3 = Buffer.from("hello");
test("Buffer.from string creates buffer with correct length", buf3.length === 5);
test("Buffer.from string has correct first byte", buf3[0] === 104); // 'h' = 104

// Test 5: Buffer indexing set/get
buf1[0] = 42;
buf1[1] = 255;
test("Buffer set/get works correctly", buf1[0] === 42);
test("Buffer can store 255", buf1[1] === 255);

// Test 6: buffer.fill - simple case
const buf4 = Buffer.alloc(5);
buf4.fill(7);
const allSevens = buf4[0] === 7 && buf4[1] === 7 && buf4[2] === 7 && buf4[3] === 7 && buf4[4] === 7;
test("buffer.fill fills entire buffer", allSevens);

// Test 7: buffer.fill with range
const buf5 = Buffer.alloc(10);
buf5.fill(9, 2, 5);
test("buffer.fill(9, 2, 5) - before range is 0", buf5[1] === 0);
test("buffer.fill(9, 2, 5) - start of range is 9", buf5[2] === 9);
test("buffer.fill(9, 2, 5) - end of range is 9", buf5[4] === 9);
test("buffer.fill(9, 2, 5) - after range is 0", buf5[5] === 0);

// Test 8: buffer.slice
const buf6 = Buffer.alloc(10);
buf6[0] = 0; buf6[1] = 1; buf6[2] = 2; buf6[3] = 3; buf6[4] = 4;
buf6[5] = 5; buf6[6] = 6; buf6[7] = 7; buf6[8] = 8; buf6[9] = 9;
const sliced = buf6.slice(2, 5);
test("buffer.slice length is correct", sliced.length === 3);
test("buffer.slice copies correct values", sliced[0] === 2 && sliced[1] === 3 && sliced[2] === 4);

// Test 9: buffer.subarray (same as slice for now)
const subbed = buf6.subarray(3, 7);
test("buffer.subarray length is correct", subbed.length === 4);
test("buffer.subarray copies correct values", subbed[0] === 3 && subbed[3] === 6);

// Test 10: buffer.copy
const src = Buffer.alloc(5);
src[0] = 1; src[1] = 2; src[2] = 3; src[3] = 4; src[4] = 5;
const dst = Buffer.alloc(10);
const copied = src.copy(dst, 2);
test("buffer.copy returns bytes copied", copied === 5);
test("buffer.copy to correct position", dst[2] === 1 && dst[3] === 2 && dst[6] === 5);
test("buffer.copy leaves other bytes unchanged", dst[0] === 0 && dst[1] === 0);

// Test 11: buffer.copy with source range
const src2 = Buffer.alloc(10);
src2[0] = 0; src2[1] = 2; src2[2] = 4; src2[3] = 6; src2[4] = 8; src2[5] = 10;
const dst2 = Buffer.alloc(10);
src2.copy(dst2, 0, 3, 6); // copy bytes 3,4,5 (values 6,8,10) to dst2 starting at 0
test("buffer.copy with range - first byte", dst2[0] === 6);
test("buffer.copy with range - second byte", dst2[1] === 8);
test("buffer.copy with range - third byte", dst2[2] === 10);
test("buffer.copy with range - rest unchanged", dst2[3] === 0);

// Test 12: Buffer.concat
const a = Buffer.alloc(3);
const b = Buffer.alloc(2);
a[0] = 1; a[1] = 2; a[2] = 3;
b[0] = 4; b[1] = 5;
const concatenated = Buffer.concat([a, b]);
test("Buffer.concat length is sum", concatenated.length === 5);
test("Buffer.concat has correct values", 
    concatenated[0] === 1 && concatenated[2] === 3 && concatenated[3] === 4 && concatenated[4] === 5);

// Test 13: Buffer.isBuffer
test("Buffer.isBuffer returns true for buffer", Buffer.isBuffer(buf1) === true);

// Test 14: Negative indices in slice
const buf7 = Buffer.alloc(10);
buf7[0] = 0; buf7[1] = 1; buf7[2] = 2; buf7[3] = 3; buf7[4] = 4;
buf7[5] = 5; buf7[6] = 6; buf7[7] = 7; buf7[8] = 8; buf7[9] = 9;
const negSlice = buf7.slice(-3, 10); // Should get last 3 bytes (7, 8, 9)
test("buffer.slice with negative index - length", negSlice.length === 3);
test("buffer.slice with negative index - values", negSlice[0] === 7 && negSlice[2] === 9);

// Test 15: buffer.toString
const strBuf = Buffer.from("world");
const backToStr = strBuf.toString();
test("buffer.toString converts back to string", backToStr === "world");

// Summary
console.log("");
console.log("=== Buffer Tests Complete ===");
console.log("Passed: " + passed);
console.log("Failed: " + failed);

if (failed === 0) {
    console.log("All tests passed!");
}
