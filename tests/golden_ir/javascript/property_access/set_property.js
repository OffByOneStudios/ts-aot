// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_object_set_dynamic
// OUTPUT: 42

var obj = {};
    obj.foo = 42;
    console.log(obj.foo);
