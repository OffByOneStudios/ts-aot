// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_strict_eq
// OUTPUT: false

function user_main() {
    if (1 === "1") {
        console.log("true");
    } else {
        console.log("false");
    }
    return 0;
}
