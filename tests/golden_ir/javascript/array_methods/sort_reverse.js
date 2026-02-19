// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 5,4,3,2,1

var arr = [3, 1, 4, 5, 2];
arr.sort();
arr.reverse();
console.log(arr.join(","));
