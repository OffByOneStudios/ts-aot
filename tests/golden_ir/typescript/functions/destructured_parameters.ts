// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// Test: Destructured parameters in function signature
// OUTPUT: 3

function add({ a, b }: { a: number, b: number }): number {
    return a + b;
}

console.log(add({ a: 1, b: 2 }));
