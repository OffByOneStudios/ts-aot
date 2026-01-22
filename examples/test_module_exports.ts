// Test module.exports functionality

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic exports object exists
    if (typeof exports === 'object') {
        console.log("PASS: exports is an object");
        passed++;
    } else {
        console.log("FAIL: exports is not an object");
        failed++;
    }

    // Test 2: module.exports exists
    if (typeof module === 'object' && typeof module.exports === 'object') {
        console.log("PASS: module.exports exists");
        passed++;
    } else {
        console.log("FAIL: module.exports does not exist");
        failed++;
    }

    // Test 3: exports === module.exports identity
    if (exports === module.exports) {
        console.log("PASS: exports === module.exports");
        passed++;
    } else {
        console.log("FAIL: exports !== module.exports");
        failed++;
    }

    // Test 4: Setting property on exports
    exports.testValue = 42;
    if (exports.testValue === 42) {
        console.log("PASS: Can set property on exports");
        passed++;
    } else {
        console.log("FAIL: Cannot set property on exports");
        failed++;
    }

    // Test 5: Property reflects in module.exports
    if (module.exports.testValue === 42) {
        console.log("PASS: exports change reflects in module.exports");
        passed++;
    } else {
        console.log("FAIL: exports change does not reflect in module.exports");
        failed++;
    }

    // Test 6: Setting property via module.exports
    module.exports.anotherValue = "hello";
    if (module.exports.anotherValue === "hello") {
        console.log("PASS: Can set property via module.exports");
        passed++;
    } else {
        console.log("FAIL: Cannot set property via module.exports");
        failed++;
    }

    // Test 7: module.exports property reflects in exports
    if (exports.anotherValue === "hello") {
        console.log("PASS: module.exports change reflects in exports");
        passed++;
    } else {
        console.log("FAIL: module.exports change does not reflect in exports");
        failed++;
    }

    // Test 8: Reassigning module.exports entirely
    const myFunc = function(): number { return 123; };
    module.exports = myFunc;
    if (typeof module.exports === 'function') {
        console.log("PASS: module.exports can be reassigned to function");
        passed++;
    } else {
        console.log("FAIL: module.exports cannot be reassigned to function");
        failed++;
    }

    // Test 9: Calling the reassigned function
    if (typeof module.exports === 'function') {
        const result = (module.exports as any)();
        if (result === 123) {
            console.log("PASS: Reassigned module.exports function works");
            passed++;
        } else {
            console.log("FAIL: Reassigned module.exports function returns wrong value");
            failed++;
        }
    } else {
        console.log("SKIP: Cannot test function call (module.exports is not a function)");
        failed++;
    }

    console.log(`Results: ${passed} passed, ${failed} failed`);
    return failed > 0 ? 1 : 0;
}
