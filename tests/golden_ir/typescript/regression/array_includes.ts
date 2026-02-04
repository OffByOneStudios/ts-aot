// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Array.includes() with primitive values

const arr = [1, 2, 3, 4, 5];

console.log(arr.includes(3));  // Should print: true
console.log(arr.includes(6));  // Should print: false

// OUTPUT: true
// OUTPUT: false
// CHECK: define
// CHECK: ts_array_create_sized
