// Test util.types.isBooleanObject(), isNumberObject(), isStringObject(), isBoxedPrimitive()
import * as util from 'util';

function user_main(): number {
    console.log("=== util.types Boxed Primitive Checks ===");
    console.log("");
    let failures = 0;

    // Test 1: isBooleanObject with new Boolean(true)
    const boolObj = new Boolean(true);
    const result1 = util.types.isBooleanObject(boolObj);
    if (result1 === true) {
        console.log("PASS: new Boolean(true) detected as BooleanObject");
    } else {
        console.log("FAIL: Expected true for new Boolean(true), got: " + result1);
        failures++;
    }

    // Test 2: isBooleanObject with primitive boolean
    const primBool = true;
    const result2 = util.types.isBooleanObject(primBool);
    if (result2 === false) {
        console.log("PASS: Primitive boolean NOT detected as BooleanObject");
    } else {
        console.log("FAIL: Expected false for primitive boolean, got: " + result2);
        failures++;
    }

    // Test 3: isNumberObject with new Number(42)
    const numObj = new Number(42);
    const result3 = util.types.isNumberObject(numObj);
    if (result3 === true) {
        console.log("PASS: new Number(42) detected as NumberObject");
    } else {
        console.log("FAIL: Expected true for new Number(42), got: " + result3);
        failures++;
    }

    // Test 4: isNumberObject with primitive number
    const primNum = 42;
    const result4 = util.types.isNumberObject(primNum);
    if (result4 === false) {
        console.log("PASS: Primitive number NOT detected as NumberObject");
    } else {
        console.log("FAIL: Expected false for primitive number, got: " + result4);
        failures++;
    }

    // Test 5: isStringObject with new String("hello")
    const strObj = new String("hello");
    const result5 = util.types.isStringObject(strObj);
    if (result5 === true) {
        console.log("PASS: new String('hello') detected as StringObject");
    } else {
        console.log("FAIL: Expected true for new String('hello'), got: " + result5);
        failures++;
    }

    // Test 6: isStringObject with primitive string
    const primStr = "hello";
    const result6 = util.types.isStringObject(primStr);
    if (result6 === false) {
        console.log("PASS: Primitive string NOT detected as StringObject");
    } else {
        console.log("FAIL: Expected false for primitive string, got: " + result6);
        failures++;
    }

    // Test 7: isBoxedPrimitive with various boxed types
    const result7a = util.types.isBoxedPrimitive(boolObj);
    const result7b = util.types.isBoxedPrimitive(numObj);
    const result7c = util.types.isBoxedPrimitive(strObj);
    if (result7a === true && result7b === true && result7c === true) {
        console.log("PASS: All boxed primitives detected by isBoxedPrimitive");
    } else {
        console.log("FAIL: isBoxedPrimitive should return true for boxed primitives");
        console.log("  Boolean: " + result7a + ", Number: " + result7b + ", String: " + result7c);
        failures++;
    }

    // Test 8: isBoxedPrimitive with plain objects
    const plainObj = { foo: "bar" };
    const result8 = util.types.isBoxedPrimitive(plainObj);
    if (result8 === false) {
        console.log("PASS: Plain object NOT detected as BoxedPrimitive");
    } else {
        console.log("FAIL: Expected false for plain object, got: " + result8);
        failures++;
    }

    // Test 9: isBoxedPrimitive with primitives
    const result9a = util.types.isBoxedPrimitive(true);
    const result9b = util.types.isBoxedPrimitive(42);
    const result9c = util.types.isBoxedPrimitive("hello");
    if (result9a === false && result9b === false && result9c === false) {
        console.log("PASS: Primitives NOT detected as BoxedPrimitive");
    } else {
        console.log("FAIL: isBoxedPrimitive should return false for primitives");
        console.log("  boolean: " + result9a + ", number: " + result9b + ", string: " + result9c);
        failures++;
    }

    console.log("");
    console.log("=== Summary ===");
    if (failures === 0) {
        console.log("All tests passed!");
    } else {
        console.log("Failures: " + failures);
    }

    return failures;
}
