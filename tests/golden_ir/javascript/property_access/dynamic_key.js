// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_object_get_dynamic
// OUTPUT: 42

var obj = { foo: 42, bar: 100 };
    var key = "foo";
    var val = obj[key];
    console.log(val);
