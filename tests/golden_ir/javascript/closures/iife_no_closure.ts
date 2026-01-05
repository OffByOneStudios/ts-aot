// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK-NOT: ts_cell_create
// OUTPUT: 42

function user_main() {
    var result = (function() { return 42; })();
    console.log(result);
    return 0;
}
