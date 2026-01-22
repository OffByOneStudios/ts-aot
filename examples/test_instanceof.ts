// Test instanceof operator

class Dog {
    name: string;
    breed: string;
    constructor(name: string, breed: string) {
        this.name = name;
        this.breed = breed;
    }
    bark(): string {
        return `${this.name} barks!`;
    }
}

class Cat {
    name: string;
    color: string;
    constructor(name: string, color: string) {
        this.name = name;
        this.color = color;
    }
    meow(): string {
        return `${this.name} meows!`;
    }
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic instanceof with class
    const dog = new Dog("Rex", "German Shepherd");
    if (dog instanceof Dog) {
        console.log("PASS: dog instanceof Dog");
        passed++;
    } else {
        console.log("FAIL: dog instanceof Dog");
        failed++;
    }

    // Test 2: instanceof negative case
    const cat = new Cat("Whiskers", "orange");
    if (!(cat instanceof Dog)) {
        console.log("PASS: cat not instanceof Dog");
        passed++;
    } else {
        console.log("FAIL: cat should not be instanceof Dog");
        failed++;
    }

    // Test 3: instanceof with type narrowing
    const animals: any[] = [dog, cat];
    let dogCount = 0;
    let catCount = 0;
    for (let i = 0; i < animals.length; i++) {
        const a = animals[i];
        if (a instanceof Dog) {
            dogCount++;
        }
        if (a instanceof Cat) {
            catCount++;
        }
    }
    if (dogCount === 1 && catCount === 1) {
        console.log("PASS: instanceof in loop");
        passed++;
    } else {
        console.log("FAIL: instanceof in loop - dogs: " + dogCount + ", cats: " + catCount);
        failed++;
    }

    // Test 4: instanceof with array
    const arr: number[] = [1, 2, 3];
    if (arr instanceof Array) {
        console.log("PASS: array instanceof Array");
        passed++;
    } else {
        console.log("FAIL: array instanceof Array");
        failed++;
    }

    console.log(`Results: ${passed} passed, ${failed} failed`);
    return failed > 0 ? 1 : 0;
}
