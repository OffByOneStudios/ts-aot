// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_at
// OUTPUT: 10
// OUTPUT: 50
// OUTPUT: 50
// OUTPUT: 10

const arr = [10, 20, 30, 40, 50];
console.log(arr.at(0));
console.log(arr.at(4));
console.log(arr.at(-1));
console.log(arr.at(-5));
