// Simple interface property access test

interface Person {
    name: string;
    age: number;
}

function user_main(): number {
    const alice: Person = { name: "Alice", age: 30 };
    console.log(alice.name);
    console.log(alice.age);
    return 0;
}
