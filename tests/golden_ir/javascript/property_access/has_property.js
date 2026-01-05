// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// OUTPUT: true
// OUTPUT: false

var obj = { foo: 42 };
    console.log(obj.hasOwnProperty('foo'));
    console.log(obj.hasOwnProperty('bar'));
