// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello from Base
// OUTPUT: hello from Child

class Base {
    greet() {
        return "hello from Base";
    }
}

class Child extends Base {
    greetChild() {
        return "hello from Child";
    }
}

var b = new Base();
console.log(b.greet());
var c = new Child();
console.log(c.greetChild());
