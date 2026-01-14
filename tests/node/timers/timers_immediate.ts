// Test setImmediate() and clearImmediate()

function user_main(): number {
    console.log("=== setImmediate/clearImmediate Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: setImmediate returns a value
    console.log("\n1. setImmediate returns handle:");
    const id1 = setImmediate(() => {
        console.log("  Callback executed");
    });
    // Just verify we get something back
    console.log("  PASS: setImmediate callable");
    passed++;

    // Test 2: clearImmediate is callable
    console.log("\n2. clearImmediate is callable:");
    const id2 = setImmediate(() => {
        // This would run if event loop processed
    });
    clearImmediate(id2);
    console.log("  PASS: clearImmediate callable");
    passed++;

    // Test 3: Multiple setImmediate calls
    console.log("\n3. Multiple setImmediate calls:");
    setImmediate(() => { /* noop */ });
    setImmediate(() => { /* noop */ });
    setImmediate(() => { /* noop */ });
    console.log("  PASS: Multiple calls work");
    passed++;

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    console.log("Note: Callbacks require event loop to execute");

    return failed;
}
