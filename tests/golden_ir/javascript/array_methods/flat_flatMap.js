// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1,2,3,4

var arr = [[1, 2], [3, 4]];
var flattened = arr.flat();
console.log(flattened.join(","));
