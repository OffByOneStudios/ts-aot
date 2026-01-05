// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_object_get_dynamic
// OUTPUT: 30

function user_main() {
    var arr = [10, 20, 30, 40];
    var i = 2;
    var val = arr[i];
    console.log(val);
    return 0;
}
