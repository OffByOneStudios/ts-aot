// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 42

console.log(hoisted());

function hoisted(): number {
    return 42;
}
