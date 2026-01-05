// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// OUTPUT: 3

var obj = { a: 1, b: 2, c: 3 };
    console.log(Object.keys(obj).length);
