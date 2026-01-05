// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_object_get_dynamic
// OUTPUT: 42

var obj = { foo: 42 };
    var val = obj.foo;
    console.log(val);
