// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_map_create
// CHECK: ts_object_set_dynamic
// OUTPUT: 42

const x = 42;
const obj = { x };
console.log(obj.x);
