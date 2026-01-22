// Test function with optional parameters (simulates overload behavior)

function greet(name: string, age?: number): string {
    if (age !== undefined) {
        return `Hello ${name}, you are ${age} years old`;
    }
    return `Hello ${name}`;
}

class Calculator {
    add(a: number, b: number): number {
        return a + b;
    }
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Function - single arg (uses undefined for optional)
    const greeting1 = greet("Alice", undefined);
    if (greeting1 === "Hello Alice") {
        console.log("PASS: function with undefined");
        passed++;
    } else {
        console.log("FAIL: function with undefined: " + greeting1);
        failed++;
    }

    // Test 2: Function - two args
    const greeting2 = greet("Bob", 25);
    if (greeting2 === "Hello Bob, you are 25 years old") {
        console.log("PASS: function two args");
        passed++;
    } else {
        console.log("FAIL: function two args: " + greeting2);
        failed++;
    }

    // Test 3: Method - two args
    const calc = new Calculator();
    const result = calc.add(3, 7);
    if (result === 10) {
        console.log("PASS: method two args");
        passed++;
    } else {
        console.log("FAIL: method two args: " + result);
        failed++;
    }

    console.log(`Results: ${passed} passed, ${failed} failed`);
    return failed > 0 ? 1 : 0;
}
