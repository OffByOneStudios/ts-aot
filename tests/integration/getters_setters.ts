class Person {
    private _name: string;
    private _age: number;

    constructor(name: string, age: number) {
        this._name = name;
        this._age = age;
    }

    get name(): string {
        return this._name;
    }

    set name(value: string) {
        this._name = value;
    }

    get age(): number {
        return this._age;
    }

    set age(value: number) {
        if (value >= 0) {
            this._age = value;
        }
    }
}

function main() {
    const p = new Person("Alice", 30);
    console.log(p.name);
    console.log(p.age);

    p.name = "Bob";
    p.age = 35;
    console.log(p.name);
    console.log(p.age);

    p.age = -5; // Should not change age
    console.log(p.age);
}
