// Simple test for type predicates (is keyword)

// Type predicate function
function isString(value: any): value is string {
    return typeof value === "string";
}

function isNumber(value: any): value is number {
    return typeof value === "number";
}

function user_main(): number {
    console.log("=== Type Predicate Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: isString with string
    console.log("\n1. isString('hello'):");
    const a: any = "hello";
    if (isString(a)) {
        console.log("isString returned true for string");
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 2: isString with number
    console.log("\n2. isString(42):");
    const b: any = 42;
    if (isString(b)) {
        console.log("FAIL");
        failed++;
    } else {
        console.log("isString returned false for number");
        console.log("PASS");
        passed++;
    }

    // Test 3: isNumber
    console.log("\n3. isNumber(3.14):");
    const c: any = 3.14;
    if (isNumber(c)) {
        console.log("isNumber returned true for number");
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 4: isNumber with string
    console.log("\n4. isNumber('hi'):");
    const d: any = "hi";
    if (isNumber(d)) {
        console.log("FAIL");
        failed++;
    } else {
        console.log("isNumber returned false for string");
        console.log("PASS");
        passed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");

    return failed;
}
