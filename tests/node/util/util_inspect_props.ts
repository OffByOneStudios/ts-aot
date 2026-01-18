// Test util.inspect.custom and util.inspect.defaultOptions
import * as util from 'util';

// Use object to avoid closure issues
const stats = { passed: 0, failed: 0 };

function test(name: string, fn: () => boolean): void {
    try {
        if (fn()) {
            console.log("PASS: " + name);
            stats.passed = stats.passed + 1;
        } else {
            console.log("FAIL: " + name);
            stats.failed = stats.failed + 1;
        }
    } catch (e) {
        console.log("FAIL: " + name + " (exception)");
        stats.failed = stats.failed + 1;
    }
}

function user_main(): number {
    console.log("Testing util.inspect properties");
    console.log("================================");
    console.log("");

    // Test 1: util.inspect.custom exists and is a Symbol
    test("util.inspect.custom is a Symbol", () => {
        const customSymbol = util.inspect.custom;
        return typeof customSymbol === "symbol";
    });

    // Test 2: util.inspect.defaultOptions exists
    test("util.inspect.defaultOptions exists", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return typeof defaultOptions === "object" && defaultOptions !== null;
    });

    // Test 3: defaultOptions has depth property
    test("defaultOptions has depth property", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return "depth" in defaultOptions;
    });

    // Test 4: defaultOptions.depth is a number
    test("defaultOptions.depth is 2", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return defaultOptions.depth === 2;
    });

    // Test 5: defaultOptions has showHidden property
    test("defaultOptions has showHidden property", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return "showHidden" in defaultOptions;
    });

    // Test 6: defaultOptions.showHidden is false by default
    test("defaultOptions.showHidden is false", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return defaultOptions.showHidden === false;
    });

    // Test 7: defaultOptions has colors property
    test("defaultOptions has colors property", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return "colors" in defaultOptions;
    });

    // Test 8: defaultOptions.colors is false by default
    test("defaultOptions.colors is false", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return defaultOptions.colors === false;
    });

    // Test 9: defaultOptions has maxArrayLength property
    test("defaultOptions has maxArrayLength property", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return "maxArrayLength" in defaultOptions;
    });

    // Test 10: defaultOptions.maxArrayLength is 100 by default
    test("defaultOptions.maxArrayLength is 100", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return defaultOptions.maxArrayLength === 100;
    });

    // Test 11: defaultOptions has breakLength property
    test("defaultOptions has breakLength property", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return "breakLength" in defaultOptions;
    });

    // Test 12: defaultOptions.breakLength is 80 by default
    test("defaultOptions.breakLength is 80", () => {
        const defaultOptions = util.inspect.defaultOptions;
        return defaultOptions.breakLength === 80;
    });

    console.log("");
    console.log("Results: " + stats.passed + "/" + (stats.passed + stats.failed) + " passed");
    return stats.failed > 0 ? 1 : 0;
}
