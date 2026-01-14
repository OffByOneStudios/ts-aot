// Test util module extended features
import * as util from 'util';

function user_main(): number {
    console.log("=== Util Extended Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: util.format with %s
    console.log("\n1. util.format with %s:");
    const formatted1 = util.format("Hello %s", "World");
    console.log("  util.format('Hello %s', 'World') = " + formatted1);
    if (formatted1 === "Hello World") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'Hello World')");
        failed++;
    }

    // Test 2: util.format with %d
    console.log("\n2. util.format with %d:");
    const formatted2 = util.format("Number: %d", 42);
    console.log("  util.format('Number: %d', 42) = " + formatted2);
    if (formatted2 === "Number: 42") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'Number: 42')");
        failed++;
    }

    // Test 3: util.inspect
    console.log("\n3. util.inspect:");
    const obj = { name: "test", value: 123 };
    const inspected = util.inspect(obj);
    console.log("  util.inspect({name: 'test', value: 123}) = " + inspected);
    if (inspected && inspected.length > 0) {
        console.log("  PASS (returned non-empty string)");
        passed++;
    } else {
        console.log("  FAIL (expected non-empty string)");
        failed++;
    }

    // Test 4: util.isDeepStrictEqual - equal objects
    console.log("\n4. util.isDeepStrictEqual (equal):");
    const obj1 = { a: 1, b: 2 };
    const obj2 = { a: 1, b: 2 };
    const equal1 = util.isDeepStrictEqual(obj1, obj2);
    console.log("  isDeepStrictEqual({a:1,b:2}, {a:1,b:2}) = " + equal1);
    if (equal1 === true) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected true)");
        failed++;
    }

    // Test 5: util.isDeepStrictEqual - not equal objects
    console.log("\n5. util.isDeepStrictEqual (not equal):");
    const obj3 = { a: 1, b: 3 };
    const equal2 = util.isDeepStrictEqual(obj1, obj3);
    console.log("  isDeepStrictEqual({a:1,b:2}, {a:1,b:3}) = " + equal2);
    if (equal2 === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected false)");
        failed++;
    }

    // Test 6: util.types.isMap
    console.log("\n6. util.types.isMap:");
    const map = new Map();
    const isMap = util.types.isMap(map);
    const notMap = util.types.isMap({});
    console.log("  util.types.isMap(new Map()) = " + isMap);
    console.log("  util.types.isMap({}) = " + notMap);
    if (isMap === true && notMap === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 7: util.types.isSet
    console.log("\n7. util.types.isSet:");
    const set = new Set();
    const isSet = util.types.isSet(set);
    const notSet = util.types.isSet([]);
    console.log("  util.types.isSet(new Set()) = " + isSet);
    console.log("  util.types.isSet([]) = " + notSet);
    if (isSet === true && notSet === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 8: util.types.isRegExp
    console.log("\n8. util.types.isRegExp:");
    const regex = /test/;
    const isRegExp = util.types.isRegExp(regex);
    const notRegExp = util.types.isRegExp("test");
    console.log("  util.types.isRegExp(/test/) = " + isRegExp);
    console.log("  util.types.isRegExp('test') = " + notRegExp);
    if (isRegExp === true && notRegExp === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 9: util.types.isDate
    console.log("\n9. util.types.isDate:");
    const date = new Date();
    const isDate = util.types.isDate(date);
    const notDate = util.types.isDate("2024-01-01");
    console.log("  util.types.isDate(new Date()) = " + isDate);
    console.log("  util.types.isDate('2024-01-01') = " + notDate);
    if (isDate === true && notDate === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 10: util.types.isNativeError
    console.log("\n10. util.types.isNativeError:");
    const err = new Error("test");
    const isErr = util.types.isNativeError(err);
    const notErr = util.types.isNativeError({ message: "test" });
    console.log("  util.types.isNativeError(new Error()) = " + isErr);
    console.log("  util.types.isNativeError({message:'test'}) = " + notErr);
    if (isErr === true && notErr === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All util extended tests passed!");
    }

    return failed;
}
