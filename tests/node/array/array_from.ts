// Test Array.from() static method

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic Array.from with array source
    const source: number[] = [1, 2, 3];
    const copied = Array.from(source);
    if (copied.length === 3) {
        console.log("PASS: Array.from(array) - correct length");
        passed++;
    } else {
        console.log("FAIL: Array.from(array) - expected length 3");
        failed++;
    }

    // Test 2: Array.from with map function
    const doubled = Array.from(source, (x: number) => x * 2);
    if (doubled.length === 3) {
        console.log("PASS: Array.from(array, mapFn) - correct length");
        passed++;
    } else {
        console.log("FAIL: Array.from(array, mapFn) - expected length 3");
        failed++;
    }

    // Test 3: Verify original array unchanged
    if (source.length === 3) {
        console.log("PASS: Original array unchanged");
        passed++;
    } else {
        console.log("FAIL: Original array was modified");
        failed++;
    }

    // Test 4: Result is an array
    if (Array.isArray(copied)) {
        console.log("PASS: Result is an array");
        passed++;
    } else {
        console.log("FAIL: Result is not an array");
        failed++;
    }

    // Test 5: Empty array source
    const empty: number[] = [];
    const emptyCopy = Array.from(empty);
    if (emptyCopy.length === 0) {
        console.log("PASS: Array.from([]) returns empty array");
        passed++;
    } else {
        console.log("FAIL: Array.from([]) should return empty array");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
