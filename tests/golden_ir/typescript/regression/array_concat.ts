// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Runtime panic - Null or undefined dereference
// Test: Array.concat() with multiple arrays
//
// Bug: The Array.concat() method compiles but causes a runtime panic:
// "Runtime Panic: Null or undefined dereference"
// The IR may be missing proper ts_array_concat implementation or has
// incorrect null checks.

const arr1 = [1, 2];
const arr2 = [3, 4];
const arr3 = [5, 6];

const result = arr1.concat(arr2, arr3);
console.log(result.length);

// OUTPUT: 6
// CHECK: define
// CHECK: ts_array_create_specialized
