// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 7
// OUTPUT: 30
// OUTPUT: 1

var a = 10;
var b = 3;
console.log(a - b);
console.log(a * b);
console.log(a % b);
