// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// OUTPUT: Testing keyof operator...
// OUTPUT: Direct name: Alice
// OUTPUT: Direct age: 30
// OUTPUT: getPersonProperty name: Alice
// OUTPUT: getPersonProperty age: 30
// OUTPUT: Dynamic name: Alice
// OUTPUT: All keyof tests passed!

// Test: keyof operator

type Person = { name: string; age: number };
type PersonKeys = keyof Person; // Should be "name" | "age"

// Test keyof with explicit key type
function getPersonProperty(person: Person, key: keyof Person): string | number {
    return person[key];
}

function user_main(): number {
    console.log("Testing keyof operator...");

    const person: Person = { name: "Alice", age: 30 };

    // Test 1: Direct property access
    console.log("Direct name: " + person.name);
    console.log("Direct age: " + person.age);

    // Test 2: keyof Person used in function signature
    const name = getPersonProperty(person, "name");
    console.log("getPersonProperty name: " + name);
    const age = getPersonProperty(person, "age");
    console.log("getPersonProperty age: " + age);

    // Test 3: Dynamic key with cast
    const key: keyof Person = "name";
    const dynName = person[key];
    console.log("Dynamic name: " + dynName);

    console.log("All keyof tests passed!");
    return 0;
}
