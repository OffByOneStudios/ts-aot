// Test parameter destructuring patterns (ES6)

// Test 1: Simple object destructuring parameter
function add({ a, b }: { a: number, b: number }): number {
    return a + b;
}

// Test 2: Renamed destructured parameters
function rename({ x: first, y: second }: { x: number, y: number }): number {
    return first * second;
}

// Test 3: String destructured parameters
function greet({ name, greeting }: { name: string, greeting: string }): string {
    return greeting + ", " + name + "!";
}

// Test 4: Multiple parameters with one destructured
function mixed(prefix: string, { x, y }: { x: number, y: number }): string {
    return prefix + ": " + (x + y);
}

// Test 5: Deeply nested object destructuring in parameter
function nestedExtract({ a: { b: { c } } }: { a: { b: { c: number } } }): number {
    return c * 2;
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Simple object destructuring parameter
    const result1 = add({ a: 1, b: 2 });
    if (result1 === 3) {
        console.log("PASS: Simple object destructuring parameter");
        passed++;
    } else {
        console.log("FAIL: Simple object destructuring parameter - got " + result1);
        failed++;
    }

    // Test 2: Renamed destructured parameters
    const result2 = rename({ x: 3, y: 4 });
    if (result2 === 12) {
        console.log("PASS: Renamed destructured parameters");
        passed++;
    } else {
        console.log("FAIL: Renamed destructured parameters - got " + result2);
        failed++;
    }

    // Test 3: String destructured parameters
    const result3 = greet({ name: "World", greeting: "Hello" });
    if (result3 === "Hello, World!") {
        console.log("PASS: String destructured parameters");
        passed++;
    } else {
        console.log("FAIL: String destructured parameters - got " + result3);
        failed++;
    }

    // Test 4: Multiple parameters with one destructured
    const result4 = mixed("Sum", { x: 10, y: 20 });
    if (result4 === "Sum: 30") {
        console.log("PASS: Mixed regular and destructured parameters");
        passed++;
    } else {
        console.log("FAIL: Mixed regular and destructured parameters - got " + result4);
        failed++;
    }

    // Test 5: Deeply nested object destructuring in parameter
    const result5 = nestedExtract({ a: { b: { c: 7 } } });
    if (result5 === 14) {
        console.log("PASS: Deeply nested destructured parameter");
        passed++;
    } else {
        console.log("FAIL: Deeply nested destructured parameter - got " + result5);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
