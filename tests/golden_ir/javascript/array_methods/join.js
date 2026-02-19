// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1-2-3
// OUTPUT: 1,2,3

var arr = [1, 2, 3];
console.log(arr.join("-"));
console.log(arr.join(","));
