// Test util.isDeepStrictEqual()
import * as util from 'util';

function test(name: string, condition: boolean): void {
    if (condition) {
        console.log("PASS: " + name);
    } else {
        console.log("FAIL: " + name);
    }
}

function user_main(): number {
    // Test 1: Same object reference
    const obj1 = { a: 1 };
    test("Same reference is equal", util.isDeepStrictEqual(obj1, obj1) === true);

    // Test 2: Equal objects with same values
    const obj2 = { a: 1 };
    const obj3 = { a: 1 };
    test("Equal objects are equal", util.isDeepStrictEqual(obj2, obj3) === true);

    // Test 3: Different objects
    const obj4 = { a: 1 };
    const obj5 = { a: 2 };
    test("Different objects are not equal", util.isDeepStrictEqual(obj4, obj5) === false);

    // Test 4: Nested objects equal
    const nested1 = { a: { b: 1 } };
    const nested2 = { a: { b: 1 } };
    test("Nested equal objects", util.isDeepStrictEqual(nested1, nested2) === true);

    // Test 5: Nested objects different
    const nested3 = { a: { b: 1 } };
    const nested4 = { a: { b: 2 } };
    test("Nested different objects", util.isDeepStrictEqual(nested3, nested4) === false);

    // Test 6: Arrays equal
    const arr1 = [1, 2, 3];
    const arr2 = [1, 2, 3];
    test("Equal arrays", util.isDeepStrictEqual(arr1, arr2) === true);

    // Test 7: Arrays different
    const arr3 = [1, 2, 3];
    const arr4 = [1, 2, 4];
    test("Different arrays", util.isDeepStrictEqual(arr3, arr4) === false);

    // Test 8: Primitives equal
    test("Equal numbers", util.isDeepStrictEqual(42, 42) === true);

    // Test 9: Primitives different
    test("Different numbers", util.isDeepStrictEqual(42, 43) === false);

    // Test 10: Strings equal
    test("Equal strings", util.isDeepStrictEqual("hello", "hello") === true);

    // Test 11: Strings different
    test("Different strings", util.isDeepStrictEqual("hello", "world") === false);

    // Test 12: null vs null
    test("null equals null", util.isDeepStrictEqual(null, null) === true);

    // Test 13: object vs null
    test("object not equal to null", util.isDeepStrictEqual({}, null) === false);

    return 0;
}
