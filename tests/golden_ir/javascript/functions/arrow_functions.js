// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 25

var square = function(x) { return x * x; };
console.log(square(5));
