// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// OUTPUT: true

var obj = { a: 1, b: 2 };
    console.log('a' in obj);
