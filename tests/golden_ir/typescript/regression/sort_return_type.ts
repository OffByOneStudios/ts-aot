// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// CHECK: @ts_array_sort
// OUTPUT: value: 1

// This test verifies that Array.sort() with a callback returns the array type,
// not void. Previously, the return type was incorrectly inferred as void,
// causing a compiler crash when the result was passed to another function.
// Regression test for sort() return type inference bug.

function helper(arr: number[]): number {
    return arr[0];
}

function user_main(): number {
    const data: number[] = [3, 1, 2];
    const sorted = data.sort((a, b) => a - b);
    const value = helper(sorted);
    console.log("value: " + value);
    return 0;
}
