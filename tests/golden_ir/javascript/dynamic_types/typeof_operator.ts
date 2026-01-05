// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_typeof
// OUTPUT: number

function user_main() {
    console.log(typeof 42);
    return 0;
}
