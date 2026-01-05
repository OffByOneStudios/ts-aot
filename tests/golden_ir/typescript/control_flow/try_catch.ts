// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Try-catch-finally not implemented
// CHECK: define {{.*}} @user_main
// OUTPUT: caught
// OUTPUT: finally

function user_main(): number {
    try {
        throw new Error("test");
    } catch (e) {
        console.log("caught");
    } finally {
        console.log("finally");
    }
    return 0;
}
