// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: IIFE patterns cause compilation error
// CHECK: define {{.*}} @user_main
// OUTPUT: 42

function user_main(): number {
    const result = (function() { return 42; })();
    console.log(result);
    return 0;
}
