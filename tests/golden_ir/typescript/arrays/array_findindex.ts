// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_create_sized
// OUTPUT: 2
// OUTPUT: -1

const arr = [10, 20, 30, 40];
console.log(arr.findIndex(x => x > 25));
console.log(arr.findIndex(x => x > 100));
