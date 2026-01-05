// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Destructured parameters cause compilation error
// CHECK: define {{.*}} @user_main
// OUTPUT: 3

function add({ a, b }: { a: number, b: number }): number {
    return a + b;
}

function user_main(): number {
    console.log(add({ a: 1, b: 2 }));
    return 0;
}
