// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_create_specialized
// OUTPUT: true
// OUTPUT: false

const arr1 = [1, 3, 5, 8];
const arr2 = [1, 3, 5, 7];
console.log(arr1.some(x => x % 2 === 0));
console.log(arr2.some(x => x % 2 === 0));
