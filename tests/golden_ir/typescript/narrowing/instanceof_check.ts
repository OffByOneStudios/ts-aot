// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_instanceof
// OUTPUT: PASS: basic instanceof
// OUTPUT: PASS: negative instanceof
// OUTPUT: PASS: array instanceof

class Dog {
    name: string;
    breed: string;
    constructor(name: string, breed: string) {
        this.name = name;
        this.breed = breed;
    }
}

class Cat {
    name: string;
    color: string;
    constructor(name: string, color: string) {
        this.name = name;
        this.color = color;
    }
}

function user_main(): number {
    // Test 1: Basic instanceof
    const dog = new Dog("Rex", "Labrador");
    if (dog instanceof Dog) {
        console.log("PASS: basic instanceof");
    } else {
        console.log("FAIL: basic instanceof");
        return 1;
    }

    // Test 2: Negative case
    const cat = new Cat("Whiskers", "orange");
    if (!(cat instanceof Dog)) {
        console.log("PASS: negative instanceof");
    } else {
        console.log("FAIL: negative instanceof");
        return 1;
    }

    // Test 3: Array instanceof
    const arr: number[] = [1, 2, 3];
    if (arr instanceof Array) {
        console.log("PASS: array instanceof");
    } else {
        console.log("FAIL: array instanceof");
        return 1;
    }

    return 0;
}
