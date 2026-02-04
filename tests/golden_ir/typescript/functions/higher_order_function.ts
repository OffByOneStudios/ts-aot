// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: 15

function multiplier(factor: number) {
    return (x: number) => x * factor;
}

const triple = multiplier(3);
console.log(triple(5));
