// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_create_sized
// OUTPUT: 30

const arr = [10, 20, 30, 40];
const result = arr.find(x => x > 25);
console.log(result);
