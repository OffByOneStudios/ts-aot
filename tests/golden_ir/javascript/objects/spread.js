// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

var a = { x: 1, y: 2 };
var b = { ...a, z: 3 };
console.log(b.x);
console.log(b.y);
console.log(b.z);
