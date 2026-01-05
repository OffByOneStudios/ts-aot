// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
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
