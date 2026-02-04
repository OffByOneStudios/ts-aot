// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Array.concat() with multiple arrays

const arr1 = [1, 2];
const arr2 = [3, 4];
const arr3 = [5, 6];

const result = arr1.concat(arr2, arr3);
console.log(result.length);

// OUTPUT: 6
// CHECK: define
// CHECK: ts_array_create_sized
// CHECK: ts_array_concat
// CHECK: ts_array_concat
