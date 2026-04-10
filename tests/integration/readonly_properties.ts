class Person {
    readonly name: string;
    age: number;

    constructor(name: string, age: number) {
        this.name = name; // Allowed in constructor
        this.age = age;
    }
}

function user_main(): number {
    const p = new Person("Alice", 30);
    console.log(p.name);
    console.log("" + p.age);
    p.age = 31; // Mutable field — allowed
    console.log("" + p.age);
    // p.name = "Bob" would be a compile error (readonly)
    return p.name === "Alice" && p.age === 31 ? 0 : 1;
}
