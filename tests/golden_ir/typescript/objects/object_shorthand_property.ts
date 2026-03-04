// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 42

const x = 42;
const obj = { x };
console.log(obj.x);
