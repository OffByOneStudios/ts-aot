// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_object_get_prop
// OUTPUT: 42

var obj = { foo: 42 };
    var val = obj.foo;
    console.log(val);
