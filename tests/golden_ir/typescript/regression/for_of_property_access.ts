// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define {{.*}} @user_main
// OUTPUT: Alice is 30
// OUTPUT: Bob is 25

// Bug: Property access on for...of loop variables returned undefined.
// The compiler didn't generate property access code for ClassTypes
// (interfaces, anonymous objects) that don't have struct layouts.

interface Person {
    name: string;
    age: number;
}

function user_main(): number {
    const people: Person[] = [
        { name: "Alice", age: 30 },
        { name: "Bob", age: 25 }
    ];

    // Test for...of property access on interface-typed objects
    for (const person of people) {
        console.log(person.name + " is " + person.age);
    }

    return 0;
}
