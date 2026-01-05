// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: 42

function user_main(): number {
    var x = 42;
    console.log(x);
    return 0;
}
