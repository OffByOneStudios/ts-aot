// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_create_sized
// OUTPUT: true
// OUTPUT: false

const arr1 = [2, 4, 6, 8];
const arr2 = [1, 2, 3, 4];
console.log(arr1.every(x => x % 2 === 0));
console.log(arr2.every(x => x % 2 === 0));
