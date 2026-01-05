// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_cell_create
// OUTPUT: 52

function user_main() {
    var x = 10;
    var result = (function() { return x + 42; })();
    console.log(result);
    return 0;
}
