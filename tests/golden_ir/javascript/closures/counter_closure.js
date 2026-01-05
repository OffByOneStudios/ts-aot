// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @__ts_module_init
// CHECK: call {{.*}} @ts_cell_create
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

var counter = (function() {
        var count = 0;
        return function() {
            count = count + 1;
            return count;
        };
    })();
    console.log(counter());
    console.log(counter());
    console.log(counter());
