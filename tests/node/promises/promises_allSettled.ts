// Test Promise.allSettled()

async function user_main(): Promise<number> {
    let passed = 0;
    let failed = 0;

    // Test 1: Mix of resolved and rejected promises
    const p1 = Promise.resolve(1);
    const p2 = Promise.reject("error");
    const p3 = Promise.resolve(3);

    const results = await Promise.allSettled([p1, p2, p3]);
    if (results.length === 3) {
        console.log("PASS: allSettled() returns all results");
        passed++;
    } else {
        console.log("FAIL: allSettled() - expected 3 results, got " + results.length);
        failed++;
    }

    // Test 2: All resolved
    const p4 = Promise.resolve("a");
    const p5 = Promise.resolve("b");
    const results2 = await Promise.allSettled([p4, p5]);
    if (results2.length === 2) {
        console.log("PASS: allSettled() with all resolved");
        passed++;
    } else {
        console.log("FAIL: allSettled() all resolved - expected 2, got " + results2.length);
        failed++;
    }

    // Test 3: All rejected
    const p6 = Promise.reject("err1");
    const p7 = Promise.reject("err2");
    const results3 = await Promise.allSettled([p6, p7]);
    if (results3.length === 2) {
        console.log("PASS: allSettled() with all rejected");
        passed++;
    } else {
        console.log("FAIL: allSettled() all rejected - expected 2, got " + results3.length);
        failed++;
    }

    // Test 4: Empty array
    const results4 = await Promise.allSettled([]);
    if (results4.length === 0) {
        console.log("PASS: allSettled() with empty array");
        passed++;
    } else {
        console.log("FAIL: allSettled() empty - expected 0, got " + results4.length);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
