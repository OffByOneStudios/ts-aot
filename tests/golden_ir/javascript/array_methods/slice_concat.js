// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 2,3
// OUTPUT: 1,2,3,4,5

var arr = [1, 2, 3, 4, 5];
var sliced = arr.slice(1, 3);
console.log(sliced.join(","));
var combined = arr.slice(0, 3).concat(arr.slice(3));
console.log(combined.join(","));
