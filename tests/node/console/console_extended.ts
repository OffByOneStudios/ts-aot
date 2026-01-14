// Test console.timeLog(), console.dir(), console.count(), console.countReset()

function user_main(): number {
    console.log("=== Extended Console Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: console.timeLog()
    console.log("\n1. console.timeLog():");
    console.time("timer1");
    // Do some work
    let sum = 0;
    for (let i = 0; i < 1000; i++) {
        sum += i;
    }
    console.timeLog("timer1");  // Should print elapsed time without stopping
    console.log("  (timer still running)");
    console.timeEnd("timer1");  // Now stop it
    console.log("  PASS: timeLog works");
    passed++;

    // Test 2: console.dir()
    console.log("\n2. console.dir():");
    const obj = { name: "test", value: 42 };
    console.dir(obj);
    console.log("  PASS: dir works");
    passed++;

    // Test 3: console.count()
    console.log("\n3. console.count():");
    console.count("myCounter");  // Should print "myCounter: 1"
    console.count("myCounter");  // Should print "myCounter: 2"
    console.count("myCounter");  // Should print "myCounter: 3"
    console.log("  PASS: count works");
    passed++;

    // Test 4: console.count() with different labels
    console.log("\n4. console.count() with multiple labels:");
    console.count("a");  // a: 1
    console.count("b");  // b: 1
    console.count("a");  // a: 2
    console.count("b");  // b: 2
    console.log("  PASS: multiple counters work");
    passed++;

    // Test 5: console.countReset()
    console.log("\n5. console.countReset():");
    console.count("resetMe");    // resetMe: 1
    console.count("resetMe");    // resetMe: 2
    console.countReset("resetMe");
    console.count("resetMe");    // resetMe: 1 (after reset)
    console.log("  PASS: countReset works");
    passed++;

    // Test 6: console.trace()
    console.log("\n6. console.trace():");
    console.trace();
    console.log("  PASS: trace works");
    passed++;

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");

    return failed;
}
