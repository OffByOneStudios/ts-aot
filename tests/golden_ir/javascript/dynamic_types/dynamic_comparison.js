// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_lt
// CHECK: call {{.*}} @ts_value_gt
// OUTPUT: true
// OUTPUT: false

function user_main() {
    var a = 5;
    var b = 10;
    console.log(a < b);
    console.log(a > b);
    return 0;
}
