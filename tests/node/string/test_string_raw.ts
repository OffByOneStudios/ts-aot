// Test String.raw() - returns raw string without escape processing

function user_main(): number {
    // Test 1: Basic usage with object literal (non-tagged template usage)
    console.log("Test 1 - Basic usage:");
    const template1 = { raw: ["Hello", "World"] };
    const result1 = String.raw(template1, " ");
    if (result1 === "Hello World") {
        console.log("PASS: Basic raw template works");
    } else {
        console.log("FAIL: Expected 'Hello World', got:");
        console.log(result1);
    }

    // Test 2: Multiple substitutions
    console.log("Test 2 - Multiple substitutions:");
    const template2 = { raw: ["a", "b", "c", "d"] };
    const result2 = String.raw(template2, 1, 2, 3);
    if (result2 === "a1b2c3d") {
        console.log("PASS: Multiple substitutions work");
    } else {
        console.log("FAIL: Expected 'a1b2c3d', got:");
        console.log(result2);
    }

    // Test 3: No substitutions
    console.log("Test 3 - No substitutions:");
    const template3 = { raw: ["Hello World"] };
    const result3 = String.raw(template3);
    if (result3 === "Hello World") {
        console.log("PASS: No substitutions works");
    } else {
        console.log("FAIL: Expected 'Hello World', got:");
        console.log(result3);
    }

    // Test 4: Empty template
    console.log("Test 4 - Empty template:");
    const template4 = { raw: [] };
    const result4 = String.raw(template4);
    if (result4 === "") {
        console.log("PASS: Empty template returns empty string");
    } else {
        console.log("FAIL: Expected empty string, got:");
        console.log(result4);
    }

    // Test 5: Numeric substitutions
    console.log("Test 5 - Numeric substitutions:");
    const template5 = { raw: ["Value: ", ""] };
    const result5 = String.raw(template5, 42);
    if (result5 === "Value: 42") {
        console.log("PASS: Numeric substitution works");
    } else {
        console.log("FAIL: Expected 'Value: 42', got:");
        console.log(result5);
    }

    // Test 6: Boolean substitutions
    console.log("Test 6 - Boolean substitutions:");
    const template6 = { raw: ["Is true: ", ""] };
    const result6 = String.raw(template6, true);
    if (result6 === "Is true: true") {
        console.log("PASS: Boolean substitution works");
    } else {
        console.log("FAIL: Expected 'Is true: true', got:");
        console.log(result6);
    }

    return 0;
}
