// Test Object.getPrototypeOf

function user_main(): number {
    // Test 1: Basic object - should return undefined/null (no prototype chain in our runtime)
    const obj = { a: 1 };
    const proto = Object.getPrototypeOf(obj);

    // In our runtime, getPrototypeOf returns undefined since we don't have prototype chains
    if (proto === undefined) {
        console.log("PASS: getPrototypeOf returns undefined");
    } else {
        console.log("UNEXPECTED: getPrototypeOf returned something");
    }

    // Test 2: Array - should also return undefined
    const arr = [1, 2, 3];
    const arrProto = Object.getPrototypeOf(arr);

    if (arrProto === undefined) {
        console.log("PASS: array getPrototypeOf returns undefined");
    } else {
        console.log("UNEXPECTED: array getPrototypeOf returned something");
    }

    return 0;
}
