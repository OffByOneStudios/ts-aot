// Array element property access test

interface Person {
    name: string;
    age: number;
}

function user_main(): number {
    const people: Person[] = [
        { name: "Alice", age: 30 },
        { name: "Bob", age: 25 }
    ];

    // Get first element
    const first = people[0];
    console.log("First person:");
    console.log(first.name);
    console.log(first.age);

    return 0;
}
