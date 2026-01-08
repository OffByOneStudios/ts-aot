// Test Array.prototype.flatMap()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic flatMap - double each number and create pairs
    const arr1: number[] = [1, 2, 3];
    const result1 = arr1.flatMap((x: number) => [x, x * 2]);
    if (result1.length === 6) {
        console.log("PASS: flatMap() creates pairs - length 6");
        passed++;
    } else {
        console.log("FAIL: flatMap() pairs - expected 6, got " + result1.length);
        failed++;
    }

    // Test 2: flatMap that returns single element arrays
    const arr2: number[] = [1, 2, 3];
    const result2 = arr2.flatMap((x: number) => [x * 10]);
    if (result2.length === 3) {
        console.log("PASS: flatMap() single elements - length 3");
        passed++;
    } else {
        console.log("FAIL: flatMap() single - expected 3, got " + result2.length);
        failed++;
    }

    // Test 3: flatMap with empty arrays (filtering)
    const arr3: number[] = [1, 2, 3, 4, 5];
    const result3 = arr3.flatMap((x: number) => x % 2 === 0 ? [x] : []);
    if (result3.length === 2) {
        console.log("PASS: flatMap() filtering evens - length 2");
        passed++;
    } else {
        console.log("FAIL: flatMap() filter - expected 2, got " + result3.length);
        failed++;
    }

    // Test 4: flatMap on empty array
    const arr4: number[] = [];
    const result4 = arr4.flatMap((x: number) => [x, x]);
    if (result4.length === 0) {
        console.log("PASS: flatMap() on empty array - length 0");
        passed++;
    } else {
        console.log("FAIL: flatMap() empty - expected 0, got " + result4.length);
        failed++;
    }

    // Test 5: Verify flatMap is equivalent to map + flat
    const arr5: number[] = [1, 2];
    const flatMapped = arr5.flatMap((x: number) => [x, x + 10]);
    if (flatMapped.length === 4) {
        console.log("PASS: flatMap() equivalence - length 4");
        passed++;
    } else {
        console.log("FAIL: flatMap() equivalence - expected 4, got " + flatMapped.length);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
