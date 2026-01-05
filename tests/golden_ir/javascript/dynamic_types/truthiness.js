// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_to_bool
// OUTPUT: truthy

function user_main() {
    var x = 42;
    if (x) {
        console.log("truthy");
    } else {
        console.log("falsy");
    }
    return 0;
}
