// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Array.at() not implemented
// CHECK: define {{.*}} @user_main
// OUTPUT: 10
// OUTPUT: 50
// OUTPUT: 50
// OUTPUT: 10

function user_main(): number {
    const arr = [10, 20, 30, 40, 50];
    console.log(arr.at(0));
    console.log(arr.at(4));
    console.log(arr.at(-1));
    console.log(arr.at(-5));
    return 0;
}
