// Test Array.prototype.flatMap()

function user_main(): number {
    // Test 1: Basic flatMap - double each number and create pairs
    const arr: number[] = [1, 2, 3];
    const result = arr.flatMap((x: number) => [x, x * 2]);
    console.log("Test 1 - flatMap length: " + result.length);
    // Expected: [1, 2, 2, 4, 3, 6] -> length 6

    // Test 2: flatMap that returns single values
    const arr2: number[] = [1, 2, 3];
    const result2 = arr2.flatMap((x: number) => [x * 10]);
    console.log("Test 2 - single values: " + result2.length);
    // Expected: [10, 20, 30] -> length 3

    return 0;
}
