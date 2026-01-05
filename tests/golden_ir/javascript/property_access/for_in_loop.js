// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// OUTPUT: a
// OUTPUT: b
// OUTPUT: c

var obj = { a: 1, b: 2, c: 3 };
    for (var key in obj) {
        console.log(key);
    }
