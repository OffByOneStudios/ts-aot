// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: 6

function sum(a, b, c) {
    return a + b + c;
}
console.log(sum(1, 2, 3));
