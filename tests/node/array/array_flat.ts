// Test Array.prototype.flat()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic flat with nested arrays
    const nested: number[][] = [[1, 2], [3, 4]];
    const flat1 = nested.flat();
    if (flat1.length === 4) {
        console.log("PASS: flat() on [[1,2],[3,4]] returns length 4");
        passed++;
    } else {
        console.log("FAIL: flat() on [[1,2],[3,4]] - expected 4, got " + flat1.length);
        failed++;
    }

    // Test 2: Flat with explicit depth 1
    const nested2: number[][] = [[5, 6], [7, 8]];
    const flat2 = nested2.flat(1);
    if (flat2.length === 4) {
        console.log("PASS: flat(1) works correctly");
        passed++;
    } else {
        console.log("FAIL: flat(1) - expected 4, got " + flat2.length);
        failed++;
    }

    // Test 3: Already flat array stays the same length
    const simple: number[] = [1, 2, 3];
    const flat3 = simple.flat();
    if (flat3.length === 3) {
        console.log("PASS: flat() on already flat array");
        passed++;
    } else {
        console.log("FAIL: flat() on flat array - expected 3, got " + flat3.length);
        failed++;
    }

    // Test 4: Flat with depth 0 should not flatten
    const nested4: number[][] = [[1, 2], [3, 4]];
    const flat4 = nested4.flat(0);
    if (flat4.length === 2) {
        console.log("PASS: flat(0) does not flatten");
        passed++;
    } else {
        console.log("FAIL: flat(0) - expected 2, got " + flat4.length);
        failed++;
    }

    // Test 5: Empty nested arrays
    const emptyNested: number[][] = [[], []];
    const flat5 = emptyNested.flat();
    if (flat5.length === 0) {
        console.log("PASS: flat() on empty nested arrays");
        passed++;
    } else {
        console.log("FAIL: flat() on empty nested - expected 0, got " + flat5.length);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
