// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Runtime crash - Access violation 0xc0000005
// Test: Array.includes() with primitive values
//
// Bug: The Array.includes() method causes an access violation at runtime:
// "[ts-aot] VectoredException: code=0xc0000005 addr=..."
// This indicates a memory access error in the includes implementation,
// possibly dereferencing a null pointer or accessing invalid memory.

const arr = [1, 2, 3, 4, 5];

console.log(arr.includes(3));  // Should print: true
console.log(arr.includes(6));  // Should print: false

// OUTPUT: true
// OUTPUT: false
// CHECK: define
// CHECK: ts_array_create_specialized
