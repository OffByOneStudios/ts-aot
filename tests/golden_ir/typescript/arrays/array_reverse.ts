// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Array.reverse() doesn't modify array in place
// CHECK: define
// CHECK: ts_array_create_specialized
// OUTPUT: 3,2,1

const arr = [1, 2, 3];
arr.reverse();
console.log(arr.join(','));
