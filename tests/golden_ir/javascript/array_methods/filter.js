// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 2,4

var arr = [1, 2, 3, 4, 5];
var evens = arr.filter(function(x) { return x % 2 === 0; });
console.log(evens.join(","));
