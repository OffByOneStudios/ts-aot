// Minimal test for rest parameter type inference

function test(...arrays: number[][]): number {
    const first = arrays[0];
    return first.length;
}

const result = test([1, 2, 3], [2, 3, 4], [3, 4, 5]);
console.log(result); // Expected: 3
