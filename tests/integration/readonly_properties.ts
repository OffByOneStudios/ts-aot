class Person {
    readonly name: string;
    age: number;

    constructor(name: string, age: number) {
        this.name = name; // Allowed
        this.age = age;
    }

    updateName(newName: string) {
        this.name = newName; // Should fail
    }
}

function main() {
    const p = new Person("Alice", 30);
    console.log(p.name);
    p.age = 31;
    p.name = "Bob"; // Should fail
}
