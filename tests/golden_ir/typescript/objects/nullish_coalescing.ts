// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Nullish coalescing (??) not implemented
// CHECK: define {{.*}} @user_main
// OUTPUT: 42
// OUTPUT: default
// OUTPUT: 0

function user_main(): number {
    const val1 = 42 ?? 'default';
    const val2 = undefined ?? 'default';
    const val3 = 0 ?? 'default';
    console.log(val1);
    console.log(val2);
    console.log(val3);
    return 0;
}
