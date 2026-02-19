// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: woof

class Dog {
    constructor(name) {
        this.name = name;
    }
    speak() {
        return "woof";
    }
}
var d = new Dog("Rex");
console.log(d.speak());
