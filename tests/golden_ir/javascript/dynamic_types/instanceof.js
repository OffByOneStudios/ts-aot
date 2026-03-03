// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: instanceof operator (JS slow path)
// CHECK: define
// OUTPUT: true
// OUTPUT: true
// OUTPUT: false
// OUTPUT: true
// OUTPUT: true

class Animal {
    constructor(name) {
        this.name = name;
    }
}

class Dog extends Animal {
    constructor(name) {
        super(name);
    }
}

var dog = new Dog("Rex");
console.log(dog instanceof Dog);
console.log(dog instanceof Animal);

var cat = new Animal("Whiskers");
console.log(cat instanceof Dog);
console.log(cat instanceof Animal);

// Array instanceof
var arr = [1, 2, 3];
console.log(arr instanceof Array);
