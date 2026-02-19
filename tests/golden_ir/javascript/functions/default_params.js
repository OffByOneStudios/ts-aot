// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: hello world
// OUTPUT: hi world

function greet(name, greeting) {
    if (greeting === undefined) greeting = "hello";
    return greeting + " " + name;
}
console.log(greet("world"));
console.log(greet("world", "hi"));
