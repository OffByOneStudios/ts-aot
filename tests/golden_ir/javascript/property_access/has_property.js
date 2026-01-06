// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// OUTPUT: true
// OUTPUT: false

var obj = { foo: 42 };
    console.log(obj.hasOwnProperty('foo'));
    console.log(obj.hasOwnProperty('bar'));
