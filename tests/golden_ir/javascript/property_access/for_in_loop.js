// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// XFAIL: for-in loop outputs raw addresses instead of property names in JavaScript
// OUTPUT: a
// OUTPUT: b
// OUTPUT: c

var obj = { a: 1, b: 2, c: 3 };
    for (var key in obj) {
        console.log(key);
    }
