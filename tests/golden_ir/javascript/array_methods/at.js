// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 1
// OUTPUT: 5

var arr = [1, 2, 3, 4, 5];
console.log(arr.at(0));
console.log(arr.at(-1));
