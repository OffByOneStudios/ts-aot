// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

var arr = [1, 2, 3];
arr.forEach(function(x) { console.log(x); });
