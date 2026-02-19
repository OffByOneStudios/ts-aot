// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 15

var arr = [1, 2, 3, 4, 5];
var total = arr.reduce(function(acc, x) { return acc + x; }, 0);
console.log(total);
