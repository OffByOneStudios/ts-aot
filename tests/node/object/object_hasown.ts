// Test Object.hasOwn()

function user_main(): number {
    let passed = 0;
    let total = 0;

    // Test 1: Basic own property check
    total++;
    const obj1 = { name: "Alice", age: 30 };
    if (Object.hasOwn(obj1, "name")) {
        console.log("PASS: hasOwn detects own property 'name'");
        passed++;
    } else {
        console.log("FAIL: hasOwn should detect own property 'name'");
    }

    // Test 2: Another own property
    total++;
    if (Object.hasOwn(obj1, "age")) {
        console.log("PASS: hasOwn detects own property 'age'");
        passed++;
    } else {
        console.log("FAIL: hasOwn should detect own property 'age'");
    }

    // Test 3: Non-existent property
    total++;
    if (!Object.hasOwn(obj1, "missing")) {
        console.log("PASS: hasOwn returns false for non-existent property");
        passed++;
    } else {
        console.log("FAIL: hasOwn should return false for non-existent property");
    }

    // Test 4: Empty object
    total++;
    const obj2 = {};
    if (!Object.hasOwn(obj2, "any")) {
        console.log("PASS: hasOwn returns false for empty object");
        passed++;
    } else {
        console.log("FAIL: hasOwn should return false for empty object");
    }

    // Test 5: Numeric property values (number key as string)
    total++;
    const obj3 = { 0: "zero", 1: "one" };
    if (Object.hasOwn(obj3, "0")) {
        console.log("PASS: hasOwn detects numeric key as string");
        passed++;
    } else {
        console.log("FAIL: hasOwn should detect numeric key '0'");
    }

    // Test 6: Property with undefined value
    total++;
    const obj4: { [key: string]: number | undefined } = { undef: undefined };
    if (Object.hasOwn(obj4, "undef")) {
        console.log("PASS: hasOwn detects property with undefined value");
        passed++;
    } else {
        console.log("FAIL: hasOwn should detect property even if value is undefined");
    }

    // Test 7: Property with null value
    total++;
    const obj5: { [key: string]: string | null } = { nullVal: null };
    if (Object.hasOwn(obj5, "nullVal")) {
        console.log("PASS: hasOwn detects property with null value");
        passed++;
    } else {
        console.log("FAIL: hasOwn should detect property even if value is null");
    }

    // Test 8: Property with empty string value
    total++;
    const obj6 = { empty: "" };
    if (Object.hasOwn(obj6, "empty")) {
        console.log("PASS: hasOwn detects property with empty string value");
        passed++;
    } else {
        console.log("FAIL: hasOwn should detect property with empty string value");
    }

    console.log("---");
    console.log("Results: " + passed + "/" + total + " passed");

    return 0;
}
