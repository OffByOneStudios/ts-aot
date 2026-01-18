// Test util.debuglog() - section-based debug logging
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
    console.log("Testing util.debuglog");
    console.log("=====================");
    console.log("");

    // Test 1: debuglog returns a function
    test("debuglog returns a function", () => {
        const debug = util.debuglog("test");
        return typeof debug === "function";
    });

    // Test 2: debuglog function is callable
    test("debuglog function is callable", () => {
        const debug = util.debuglog("myapp");
        // Just call it - shouldn't throw
        debug("Hello from debuglog");
        return true;
    });

    // Test 3: debuglog with format string
    test("debuglog with format string", () => {
        const debug = util.debuglog("format");
        debug("Value: %d", 42);
        return true;
    });

    // Test 4: Multiple debuglog calls for same section
    test("Multiple calls same section", () => {
        const debug1 = util.debuglog("shared");
        const debug2 = util.debuglog("shared");
        debug1("First logger");
        debug2("Second logger");
        return true;
    });

    // Test 5: Different sections
    test("Different sections", () => {
        const debugA = util.debuglog("sectiona");
        const debugB = util.debuglog("sectionb");
        debugA("From section A");
        debugB("From section B");
        return true;
    });

    // Test 6: Section names are case-insensitive (uppercase stored)
    test("Section case handling", () => {
        const debug = util.debuglog("MixedCase");
        debug("Should work with mixed case");
        return true;
    });

    console.log("");
    console.log("Results: " + stats.passed + "/" + (stats.passed + stats.failed) + " passed");

    // Note: To actually see output, run with NODE_DEBUG=test,myapp,format,shared,sectiona,sectionb,mixedcase
    console.log("");
    console.log("Note: Set NODE_DEBUG environment variable to see debug output");
    console.log("Example: NODE_DEBUG=myapp node script.js");

    return stats.failed > 0 ? 1 : 0;
}
