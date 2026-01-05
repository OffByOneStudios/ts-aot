// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Array.slice() not implemented
// CHECK: define {{.*}} @user_main
// OUTPUT: 2,3
// OUTPUT: 2,3,4

function user_main(): number {
    const arr = [1, 2, 3, 4, 5];
    const sliced1 = arr.slice(1, 3);
    const sliced2 = arr.slice(1, 4);
    console.log(sliced1.join(','));
    console.log(sliced2.join(','));
    return 0;
}
