// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: true
// OUTPUT: false
// OUTPUT: true
// OUTPUT: true

var x = 10;
var y = 20;
console.log(x < y);
console.log(x > y);
console.log(x <= 10);
console.log(x >= 10);
