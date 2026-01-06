// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// XFAIL: Rest parameter iteration returns raw pointer instead of array elements
// CHECK: define
// OUTPUT: 6

function sum(...numbers: number[]) {
    let total = 0;
    for (const n of numbers) {
        total = total + n;
    }
    return total;
}

console.log(sum(1, 2, 3));
