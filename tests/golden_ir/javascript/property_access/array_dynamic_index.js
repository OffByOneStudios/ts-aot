// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @ts_object_get_dynamic
// XFAIL: Dynamic array index with untyped var returns empty instead of value
// OUTPUT: 30

var arr = [10, 20, 30, 40];
    var i = 2;
    var val = arr[i];
    console.log(val);
