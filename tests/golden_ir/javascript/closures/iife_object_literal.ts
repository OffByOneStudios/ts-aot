// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: JavaScript files not yet supported by compiler
// CHECK: define {{.*}} @user_main
// CHECK: call {{.*}} @ts_call_0
// OUTPUT: 1

function user_main() {
    var result = (function() { return { a: 1 }; })();
    console.log(result.a);
    return 0;
}
