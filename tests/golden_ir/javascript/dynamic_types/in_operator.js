// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// OUTPUT: true

var obj = { a: 1, b: 2 };
    console.log('a' in obj);
