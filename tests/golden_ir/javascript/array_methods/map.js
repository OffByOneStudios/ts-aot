// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 2,4,6,8,10

var arr = [1, 2, 3, 4, 5];
var doubled = arr.map(function(x) { return x * 2; });
console.log(doubled.join(","));
