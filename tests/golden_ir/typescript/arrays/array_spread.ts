// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Array spread
// CHECK: ts_array_create_sized
// OUTPUT: 4

const arr1 = [1, 2];
const arr2 = [3, 4];
const combined = [...arr1, ...arr2];
console.log(combined.length);
