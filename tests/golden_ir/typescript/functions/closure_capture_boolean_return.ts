// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: true
// OUTPUT: false
// OUTPUT: true

// Test: Closure that captures a boolean and returns it directly
function makeGetter(val: boolean): () => boolean {
    return (): boolean => {
        return val;
    };
}

console.log(makeGetter(true)());
console.log(makeGetter(false)());
console.log(makeGetter(true)());
