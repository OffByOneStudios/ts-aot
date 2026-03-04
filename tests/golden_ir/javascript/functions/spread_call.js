// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1,2,3,4,5
// OUTPUT: 1,2,3

// Test array spread syntax
var arr1 = [1, 2, 3];
var arr2 = [4, 5];
var combined = [...arr1, ...arr2];
console.log(combined.join(","));

// Test spread in array literal
var copy = [...arr1];
console.log(copy.join(","));
