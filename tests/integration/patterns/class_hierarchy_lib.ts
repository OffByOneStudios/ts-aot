// Library for class_hierarchy.ts pattern test

export abstract class Animal {
    name: string;

    constructor(name: string) {
        this.name = name;
    }

    abstract speak(): string;

    describe(): string {
        return this.name + ' says: ' + this.speak();
    }
}

export class Dog extends Animal {
    breed: string;

    constructor(name: string, breed: string) {
        super(name);
        this.breed = breed;
    }

    speak(): string {
        return 'Woof!';
    }

    describe(): string {
        return this.name + ' the ' + this.breed + ' says: ' + this.speak();
    }
}

export class Cat extends Animal {
    indoor: boolean;

    constructor(name: string, indoor: boolean) {
        super(name);
        this.indoor = indoor;
    }

    speak(): string {
        return 'Meow!';
    }
}

export function makeSound(animal: Animal): string {
    return animal.speak();
}
