// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: name: Alice
// OUTPUT: age: 30
// OUTPUT: greeting: hello from Bob

// Test: Classes that assign properties in constructor body (this.x = expr)
// should get flat object shapes with inline slots

class Person {
    constructor(name, age) {
        this.name = name;
        this.age = age;
    }
}

class Greeter {
    constructor(name) {
        this.name = name;
    }
    greet() {
        return "hello from " + this.name;
    }
}

var p = new Person("Alice", 30);
console.log("name: " + p.name);
console.log("age: " + p.age);

var g = new Greeter("Bob");
console.log("greeting: " + g.greet());
