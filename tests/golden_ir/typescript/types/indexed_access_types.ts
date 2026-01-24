// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Testing indexed access types...
// OUTPUT: Name: Alice
// OUTPUT: Age: 30
// OUTPUT: Function return: Bob
// OUTPUT: All tests passed!

// Test: Indexed access types T['prop']

type Person = {
    name: string;
    age: number;
};

// Basic indexed access types
type PersonName = Person['name'];  // string
type PersonAge = Person['age'];    // number

// Function using indexed access type as return type
function getPersonName(person: Person): PersonName {
    return person.name;
}

function user_main(): number {
    console.log("Testing indexed access types...");

    const person: Person = { name: "Alice", age: 30 };

    // Test 1: Variable with indexed access type
    const myName: PersonName = person.name;
    console.log("Name: " + myName);

    // Test 2: Another indexed access type
    const myAge: PersonAge = person.age;
    console.log("Age: " + myAge);

    // Test 3: Function return type using indexed access
    const bob: Person = { name: "Bob", age: 25 };
    const bobName = getPersonName(bob);
    console.log("Function return: " + bobName);

    console.log("All tests passed!");
    return 0;
}
