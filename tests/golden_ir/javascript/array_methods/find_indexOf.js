// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 4
// OUTPUT: 3
// OUTPUT: true

var arr = [1, 2, 3, 4, 5];
var found = arr.find(function(x) { return x > 3; });
console.log(found);
console.log(arr.indexOf(4));
console.log(arr.includes(3));
