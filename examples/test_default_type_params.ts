// Test default type parameters

// Generic class with default type parameter
class Container<T = string> {
    value: T;

    constructor(value: T) {
        this.value = value;
    }

    getValue(): T {
        return this.value;
    }
}

// Generic function with default type parameter
function createArray<T = number>(length: number, value: T): T[] {
    const arr: T[] = [];
    for (let i = 0; i < length; i++) {
        arr.push(value);
    }
    return arr;
}

// Generic interface with default type parameter
interface Box<T = any> {
    contents: T;
}

function user_main(): number {
    console.log("=== Default Type Parameter Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Using default type parameter on class
    console.log("\n1. Class with default type param (using default):");
    const strContainer = new Container("hello");
    console.log("strContainer.getValue() = " + strContainer.getValue());
    if (strContainer.getValue() === "hello") {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 2: Overriding default type parameter
    console.log("\n2. Class with explicit type param:");
    const numContainer = new Container<number>(42);
    console.log("numContainer.getValue() = " + numContainer.getValue());
    if (numContainer.getValue() === 42) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 3: Function with default type parameter
    console.log("\n3. Function with default type param:");
    const arr = createArray(3, 5);
    console.log("createArray(3, 5) = [" + arr.join(", ") + "]");
    if (arr.length === 3 && arr[0] === 5 && arr[1] === 5 && arr[2] === 5) {
        console.log("PASS");
        passed++;
    } else {
        console.log("FAIL");
        failed++;
    }

    // Test 4: Function with explicit type parameter
    console.log("\n4. Function with explicit type param:");
    const strArr = createArray<string>(2, "hi");
    console.log("createArray<string>(2, 'hi') = [" + strArr.join(", ") + "]");
    if (strArr.length === 2 && strArr[0] === "hi" && strArr[1] === "hi") {
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
