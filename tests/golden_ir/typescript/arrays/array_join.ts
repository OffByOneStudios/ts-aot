// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_create_specialized
// OUTPUT: 1,2,3

const arr = [1, 2, 3];
const result = arr.join(',');
console.log(result);
