// Test function overloads

// Function overload signatures
function greet(name: string): string;
function greet(age: number): string;
function greet(value: any): string {
    if (typeof value === "string") {
        return "Hello, " + value + "!";
    } else {
        return "You are " + value + " years old!";
    }
}

// Method overloads in a class
class Calculator {
    add(a: number, b: number): number;
    add(a: string, b: string): string;
    add(a: any, b: any): any {
        if (typeof a === "number") {
            return a + b;
        }
        return a + b;  // string concatenation
    }
}

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Function overload with string
    const greeting1 = greet("World");
    console.log("Test 1: " + greeting1);
    if (greeting1 === "Hello, World!") {
        passed++;
        console.log("PASS");
    } else {
        failed++;
        console.log("FAIL: expected 'Hello, World!' but got '" + greeting1 + "'");
    }

    // Test 2: Function overload with number
    const greeting2 = greet(25);
    console.log("Test 2: " + greeting2);
    if (greeting2 === "You are 25 years old!") {
        passed++;
        console.log("PASS");
    } else {
        failed++;
        console.log("FAIL: expected 'You are 25 years old!' but got '" + greeting2 + "'");
    }

    // Test 3: Method overload with numbers
    const calc = new Calculator();
    const sum = calc.add(5, 3);
    console.log("Test 3: 5 + 3 = " + sum);
    if (sum === 8) {
        passed++;
        console.log("PASS");
    } else {
        failed++;
        console.log("FAIL: expected 8 but got " + sum);
    }

    // Test 4: Method overload with strings
    const concat = calc.add("Hello", "World");
    console.log("Test 4: concat = " + concat);
    if (concat === "HelloWorld") {
        passed++;
        console.log("PASS");
    } else {
        failed++;
        console.log("FAIL: expected 'HelloWorld' but got '" + concat + "'");
    }

    console.log("\n" + passed + "/" + (passed + failed) + " tests passed");
    return failed;
}
