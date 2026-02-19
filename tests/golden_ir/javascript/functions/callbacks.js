// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1,4,9

var arr = [1, 2, 3];
var result = arr.map(function(x) { return x * x; });
console.log(result.join(","));
