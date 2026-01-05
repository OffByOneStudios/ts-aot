// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Object destructuring causes compilation error
// CHECK: define {{.*}} @user_main
// OUTPUT: 1
// OUTPUT: 2

function user_main(): number {
    const obj = { a: 1, b: 2, c: 3 };
    const { a, b } = obj;
    console.log(a);
    console.log(b);
    return 0;
}
