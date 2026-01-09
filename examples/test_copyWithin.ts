// Test Array.prototype.copyWithin()

function user_main(): number {
    // Test 1: Basic copyWithin
    console.log("Test 1 - Basic copyWithin:");
    const arr1: number[] = [1, 2, 3, 4, 5];
    arr1.copyWithin(0, 3);  // Copy from index 3 to index 0
    console.log(arr1.join(","));  // Should print "4,5,3,4,5"

    // Test 2: copyWithin with start and end
    console.log("Test 2 - copyWithin with start and end:");
    const arr2: number[] = [1, 2, 3, 4, 5];
    arr2.copyWithin(1, 3, 4);  // Copy from index 3 to 4 (exclusive) to index 1
    console.log(arr2.join(","));  // Should print "1,4,3,4,5"

    // Test 3: copyWithin with negative indices
    console.log("Test 3 - Negative indices:");
    const arr3: number[] = [1, 2, 3, 4, 5];
    arr3.copyWithin(-2, 0, 2);  // Copy indices 0-1 to last 2 positions
    console.log(arr3.join(","));  // Should print "1,2,3,1,2"

    // Test 4: Overlapping copy (forward)
    console.log("Test 4 - Overlapping copy (forward):");
    const arr4: number[] = [1, 2, 3, 4, 5];
    arr4.copyWithin(0, 1, 4);  // Copy indices 1-3 to index 0
    console.log(arr4.join(","));  // Should print "2,3,4,4,5"

    // Test 5: Overlapping copy (backward)
    console.log("Test 5 - Overlapping copy (backward):");
    const arr5: number[] = [1, 2, 3, 4, 5];
    arr5.copyWithin(2, 0, 3);  // Copy indices 0-2 to index 2
    console.log(arr5.join(","));  // Should print "1,2,1,2,3"

    // Test 6: Returns the array (chaining)
    console.log("Test 6 - Chaining:");
    const arr6: number[] = [1, 2, 3, 4, 5];
    const result = arr6.copyWithin(0, 3);
    console.log(result.join(","));  // Should print "4,5,3,4,5"

    return 0;
}
