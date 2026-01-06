// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: ts_array_reverse
// OUTPUT: 3,2,1

const arr = [1, 2, 3];
arr.reverse();
console.log(arr.join(','));
