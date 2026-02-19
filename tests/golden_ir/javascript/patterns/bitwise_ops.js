// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 7
// OUTPUT: 6

var a = 5;
var b = 3;
console.log(a & b);
console.log(a | b);
console.log(a ^ b);
