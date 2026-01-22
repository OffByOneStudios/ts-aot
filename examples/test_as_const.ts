// Test as const assertions
// Note: as const is primarily a compile-time type assertion
// For runtime, it should pass through unchanged

function user_main(): number {
    console.log("=== as const Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: as const on array literal
    console.log("\n1. Array literal with as const:");
    const arr = [1, 2, 3] as const;
    console.log("arr = [" + arr.join(", ") + "]");
    if (arr[0] === 1 && arr[1] === 2 && arr[2] === 3) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 2: as const on object literal
    console.log("\n2. Object literal with as const:");
    const obj = { name: "Alice", age: 30 } as const;
    console.log("obj.name = " + obj.name);
    console.log("obj.age = " + obj.age);
    if (obj.name === "Alice" && obj.age === 30) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 3: as const on primitive
    console.log("\n3. Primitive with as const:");
    const str = "hello" as const;
    console.log("str = " + str);
    if (str === "hello") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 4: Nested object with as const
    console.log("\n4. Nested object with as const:");
    const nested = {
        outer: {
            inner: 42
        }
    } as const;
    console.log("nested.outer.inner = " + nested.outer.inner);
    if (nested.outer.inner === 42) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");

    return failed;
}
