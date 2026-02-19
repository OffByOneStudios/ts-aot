// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

var arr = [1, 2, 3];
var first = arr[0];
var second = arr[1];
var third = arr[2];
console.log(first);
console.log(second);
console.log(third);
