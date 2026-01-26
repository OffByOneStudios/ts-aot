// Indexed loop property access test - this should work

interface Person {
    name: string;
    age: number;
}

function user_main(): number {
    const people: Person[] = [
        { name: "Alice", age: 30 },
        { name: "Bob", age: 25 }
    ];

    // Test indexed loop property access (this should work)
    for (let i = 0; i < people.length; i++) {
        console.log(people[i].name + " is " + people[i].age);
    }

    return 0;
}
