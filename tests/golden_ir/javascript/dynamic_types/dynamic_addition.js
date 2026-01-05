// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_value_add
// CHECK-NOT: add i64
// OUTPUT: 30

function user_main() {
    var a = 10;
    var b = 20;
    var result = a + b;
    console.log(result);
    return 0;
}
