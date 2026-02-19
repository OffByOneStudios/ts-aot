// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello world

function makeGreeting(name) {
    return "hello " + name;
}
console.log(makeGreeting("world"));
