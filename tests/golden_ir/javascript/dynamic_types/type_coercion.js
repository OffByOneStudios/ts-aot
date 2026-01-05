// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_add
// OUTPUT: 42string

function user_main() {
    var result = 42 + "string";
    console.log(result);
    return 0;
}
