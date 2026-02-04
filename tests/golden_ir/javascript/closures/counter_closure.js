// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
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
