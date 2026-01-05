// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_cell_create
// OUTPUT: 1
// OUTPUT: 2
// OUTPUT: 3

function user_main() {
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
    return 0;
}
