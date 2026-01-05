// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: hasOwnProperty returns undefined - ctx parameter not passed correctly to native function
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// OUTPUT: true
// OUTPUT: false

var obj = { foo: 42 };
    console.log(obj.hasOwnProperty('foo'));
    console.log(obj.hasOwnProperty('bar'));
