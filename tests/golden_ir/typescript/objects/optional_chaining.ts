// RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Optional chaining (?.) causes compilation error
// CHECK: define {{.*}} @user_main
// OUTPUT: 42
// OUTPUT: undefined

function user_main(): number {
    const obj1 = { nested: { value: 42 } };
    const obj2 = { nested: undefined };
    console.log(obj1.nested?.value);
    console.log(obj2.nested?.value);
    return 0;
}
