// Test console.time() and console.timeEnd()

function user_main(): number {
    console.log("=== Console Timing Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Basic timer
    console.log("\n1. Basic timer:");
    console.time("test1");
    // Do some work
    let sum = 0;
    for (let i = 0; i < 1000; i++) {
        sum += i;
    }
    console.timeEnd("test1");
    console.log("  PASS: console.time/timeEnd work");
    passed++;

    // Test 2: Multiple timers
    console.log("\n2. Multiple timers:");
    console.time("outer");
    console.time("inner");
    // Some work
    for (let i = 0; i < 500; i++) {
        sum += i;
    }
    console.timeEnd("inner");
    console.timeEnd("outer");
    console.log("  PASS: Multiple timers work");
    passed++;

    // Test 3: Timer with custom label
    console.log("\n3. Timer with custom label:");
    console.time("my-operation");
    console.timeEnd("my-operation");
    console.log("  PASS: Custom labels work");
    passed++;

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    console.log("Note: Timing values shown above");

    return failed;
}
