// Test Array.prototype.at()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    const arr: number[] = [10, 20, 30, 40, 50];

    // Test 1: Positive index
    if (arr.at(0) === 10) {
        console.log("PASS: at(0) returns first element");
        passed++;
    } else {
        console.log("FAIL: at(0) - expected 10, got " + arr.at(0));
        failed++;
    }

    // Test 2: Middle index
    if (arr.at(2) === 30) {
        console.log("PASS: at(2) returns third element");
        passed++;
    } else {
        console.log("FAIL: at(2) - expected 30, got " + arr.at(2));
        failed++;
    }

    // Test 3: Negative index -1 (last element)
    if (arr.at(-1) === 50) {
        console.log("PASS: at(-1) returns last element");
        passed++;
    } else {
        console.log("FAIL: at(-1) - expected 50, got " + arr.at(-1));
        failed++;
    }

    // Test 4: Negative index -2 (second to last)
    if (arr.at(-2) === 40) {
        console.log("PASS: at(-2) returns second to last");
        passed++;
    } else {
        console.log("FAIL: at(-2) - expected 40, got " + arr.at(-2));
        failed++;
    }

    // Test 5: Last element via positive index
    if (arr.at(4) === 50) {
        console.log("PASS: at(4) returns last element");
        passed++;
    } else {
        console.log("FAIL: at(4) - expected 50, got " + arr.at(4));
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
