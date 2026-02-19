// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello from Alice

class Greeter {
    constructor(name) {
        this.name = name;
    }
    greet() {
        return "hello from " + this.name;
    }
}
var g = new Greeter("Alice");
console.log(g.greet());
