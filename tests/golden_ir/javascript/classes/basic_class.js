// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: Alice
// OUTPUT: 30

class Person {
    constructor(name, age) {
        this.name = name;
        this.age = age;
    }
}
var p = new Person("Alice", 30);
console.log(p.name);
console.log(p.age);
