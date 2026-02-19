// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: true
// OUTPUT: false
// OUTPUT: true

var arr = [1, 2, 3, 4, 5];
console.log(arr.some(function(x) { return x > 3; }));
console.log(arr.every(function(x) { return x > 3; }));
console.log(arr.every(function(x) { return x > 0; }));
