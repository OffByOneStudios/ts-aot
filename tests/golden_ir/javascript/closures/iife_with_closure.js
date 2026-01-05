// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_cell_create
// OUTPUT: 52

var x = 10;
    var result = (function() { return x + 42; })();
    console.log(result);
