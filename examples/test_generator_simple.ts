// Test generator functions - no explicit type annotations

function* countTo(n: number) {
    for (let i = 1; i <= n; i++) {
        yield i;
    }
}

function* evenNumbers(max: number) {
    for (let i = 2; i <= max; i += 2) {
        yield i;
    }
}

function user_main(): number {
    console.log("=== Generator Function Test ===");
    let passed = 0;
    let failed = 0;

    // Test 1: for-of with generator
    console.log("\n1. for-of with countTo(5):");
    let sum = 0;
    for (const n of countTo(5)) {
        sum += n;
    }
    console.log("Sum: " + sum);
    if (sum === 15) {  // 1+2+3+4+5
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL - expected 15");
        failed++;
    }

    // Test 2: for-of with even numbers
    console.log("\n2. for-of with evenNumbers(10):");
    let sumEvens = 0;
    for (const n of evenNumbers(10)) {
        sumEvens += n;
    }
    console.log("Sum of evens: " + sumEvens);
    if (sumEvens === 30) {  // 2+4+6+8+10
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL - expected 30");
        failed++;
    }

    // Test 3: yield* delegation
    console.log("\n3. yield* delegation:");
    function* combined() {
        yield* countTo(3);
        yield* evenNumbers(6);
    }
    let total = 0;
    for (const n of combined()) {
        total += n;
    }
    console.log("Combined sum: " + total);
    // 1+2+3 + 2+4+6 = 18
    if (total === 18) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL - expected 18");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");

    return failed;
}
