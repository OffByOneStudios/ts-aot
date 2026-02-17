// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_closure_create
// OUTPUT: found 3 matches
// OUTPUT: found 0 matches

// Test: Boolean flag captured by forEach callback, mutated during iteration
function countMatches(arr: number[], threshold: number): number {
    let count = 0;
    for (let i = 0; i < arr.length; i++) {
        if (arr[i] > threshold) {
            count = count + 1;
        }
    }
    return count;
}

// This version uses a closure (forEach callback) that captures a mutable counter
function countMatchesClosure(arr: number[], threshold: number): number {
    let count = 0;
    arr.forEach((x: number) => {
        if (x > threshold) {
            count = count + 1;
        }
    });
    return count;
}

const nums = [1, 5, 3, 8, 2, 7, 4];
console.log("found " + countMatchesClosure(nums, 4) + " matches");
console.log("found " + countMatchesClosure(nums, 10) + " matches");
