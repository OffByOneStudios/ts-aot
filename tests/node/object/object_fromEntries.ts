// Test Object.fromEntries()

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic fromEntries with string keys
    const entries1: [string, number][] = [["a", 1], ["b", 2], ["c", 3]];
    const obj1 = Object.fromEntries(entries1);
    const keys1 = Object.keys(obj1);
    if (keys1.length === 3) {
        console.log("PASS: fromEntries() creates object with 3 keys");
        passed++;
    } else {
        console.log("FAIL: fromEntries() - expected 3 keys, got " + keys1.length);
        failed++;
    }

    // Test 2: Verify values are correct
    if (obj1["a"] === 1 && obj1["b"] === 2 && obj1["c"] === 3) {
        console.log("PASS: fromEntries() values are correct");
        passed++;
    } else {
        console.log("FAIL: fromEntries() values incorrect");
        failed++;
    }

    // Test 3: Empty entries array
    const entries2: [string, number][] = [];
    const obj2 = Object.fromEntries(entries2);
    const keys2 = Object.keys(obj2);
    if (keys2.length === 0) {
        console.log("PASS: fromEntries([]) creates empty object");
        passed++;
    } else {
        console.log("FAIL: fromEntries([]) - expected 0 keys, got " + keys2.length);
        failed++;
    }

    // Test 4: Single entry
    const entries3: [string, string][] = [["name", "test"]];
    const obj3 = Object.fromEntries(entries3);
    if (obj3["name"] === "test") {
        console.log("PASS: fromEntries() single entry works");
        passed++;
    } else {
        console.log("FAIL: fromEntries() single entry incorrect");
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
