// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_create_specialized
// OUTPUT: 2
// OUTPUT: -1

const arr = [10, 20, 30, 40];
console.log(arr.indexOf(30));
console.log(arr.indexOf(99));
