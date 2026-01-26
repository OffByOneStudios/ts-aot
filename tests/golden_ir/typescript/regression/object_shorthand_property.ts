// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: name: Alice
// OUTPUT: age: 30

// Regression test: Object shorthand property syntax { name, age } was previously
// producing undefined values for all properties. This test verifies the fix works.

function user_main(): number {
    const name = "Alice";
    const age = 30;

    // This syntax is broken - all properties become undefined
    const obj = { name, age };

    console.log("name: " + obj.name);
    console.log("age: " + obj.age);

    return 0;
}
