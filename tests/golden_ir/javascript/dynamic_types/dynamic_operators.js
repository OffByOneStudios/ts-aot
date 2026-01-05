// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_sub
// CHECK: call {{.*}} @ts_value_mul
// CHECK: call {{.*}} @ts_value_div
// OUTPUT: 5
// OUTPUT: 50
// OUTPUT: 2

function user_main() {
    var a = 10;
    var b = 5;
    console.log(a - b);
    console.log(a * b);
    console.log(a / b);
    return 0;
}
