// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 3

var obj = { a: 1, b: 2, c: 3 };
    console.log(Object.values(obj).length);
