// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1,2,3
// OUTPUT: 3
// OUTPUT: 1,2

var arr = [1, 2];
arr.push(3);
console.log(arr.join(","));
var last = arr.pop();
console.log(last);
console.log(arr.join(","));
