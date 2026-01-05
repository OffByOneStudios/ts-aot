// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_map_create
// CHECK: ts_object_values
// OUTPUT: 3

const obj = { a: 1, b: 2, c: 3 };
const values = Object.values(obj);
console.log(values.length);
