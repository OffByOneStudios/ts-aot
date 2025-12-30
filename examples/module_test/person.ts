// Exported class for testing cross-module class imports

export class Person {
    name: string;
    age: number;

    constructor(name: string, age: number) {
        this.name = name;
        this.age = age;
    }

    greet(): string {
        return "Hello, I'm " + this.name;
    }

    getAge(): number {
        return this.age;
    }
}
