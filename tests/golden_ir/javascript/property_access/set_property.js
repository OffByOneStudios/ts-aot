// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__module_init_{{.*}}_any
// CHECK: call {{.*}} @__ts_map_set_at
// OUTPUT: 42

var obj = {};
    obj.foo = 42;
    console.log(obj.foo);
