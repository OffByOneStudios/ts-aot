// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: IIFE patterns cause compilation error
// CHECK: define {{.*}} @user_main
// OUTPUT: 52

function user_main(): number {
    const x = 10;
    const result = (function() { return x + 42; })();
    console.log(result);
    return 0;
}
