// Test Array.of() static method

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Array.of() with no arguments creates empty array
    const arr1 = Array.of();
    if (arr1.length === 0) {
        console.log("PASS: Array.of() creates empty array");
        passed++;
    } else {
        console.log("FAIL: Array.of() should create empty array, got length " + arr1.length);
        failed++;
    }

    // Test 2: Array.of(1) creates [1]
    const arr2 = Array.of(1);
    if (arr2.length === 1 && arr2[0] === 1) {
        console.log("PASS: Array.of(1) creates [1]");
        passed++;
    } else {
        console.log("FAIL: Array.of(1) - got length " + arr2.length);
        failed++;
    }

    // Test 3: Array.of(1, 2, 3) creates [1, 2, 3]
    const arr3 = Array.of(1, 2, 3);
    if (arr3.length === 3 && arr3[0] === 1 && arr3[1] === 2 && arr3[2] === 3) {
        console.log("PASS: Array.of(1, 2, 3) creates [1, 2, 3]");
        passed++;
    } else {
        console.log("FAIL: Array.of(1, 2, 3) - got length " + arr3.length);
        failed++;
    }

    // Test 4: Array.of with strings
    const arr4 = Array.of("a", "b", "c");
    if (arr4.length === 3 && arr4[0] === "a" && arr4[1] === "b" && arr4[2] === "c") {
        console.log("PASS: Array.of('a', 'b', 'c') creates ['a', 'b', 'c']");
        passed++;
    } else {
        console.log("FAIL: Array.of with strings");
        failed++;
    }

    // Test 5: Array.of(7) differs from Array(7)
    // Array.of(7) creates [7], but Array(7) would create an array of length 7
    const arr5 = Array.of(7);
    if (arr5.length === 1 && arr5[0] === 7) {
        console.log("PASS: Array.of(7) creates [7] (length 1)");
        passed++;
    } else {
        console.log("FAIL: Array.of(7) should have length 1, got " + arr5.length);
        failed++;
    }

    // Test 6: Array.of with mixed types
    const arr6 = Array.of(1, "two", 3);
    if (arr6.length === 3 && arr6[0] === 1 && arr6[1] === "two" && arr6[2] === 3) {
        console.log("PASS: Array.of(1, 'two', 3) with mixed types");
        passed++;
    } else {
        console.log("FAIL: Array.of with mixed types");
        failed++;
    }

    // Test 7: Array.of with negative numbers
    const arr7 = Array.of(-1, -2, -3);
    if (arr7.length === 3 && arr7[0] === -1 && arr7[1] === -2 && arr7[2] === -3) {
        console.log("PASS: Array.of(-1, -2, -3) with negative numbers");
        passed++;
    } else {
        console.log("FAIL: Array.of with negative numbers");
        failed++;
    }

    // Test 8: Array.of with single undefined-like value
    const arr8 = Array.of(0);
    if (arr8.length === 1 && arr8[0] === 0) {
        console.log("PASS: Array.of(0) creates [0]");
        passed++;
    } else {
        console.log("FAIL: Array.of(0)");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
