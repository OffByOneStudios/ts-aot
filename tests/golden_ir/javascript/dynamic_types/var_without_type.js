// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_make_int
// OUTPUT: 42

function user_main() {
    var x = 42;
    console.log(x);
    return 0;
}
