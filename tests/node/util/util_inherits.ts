// Test util.inherits() - legacy prototype inheritance helper
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
    console.log("Testing util.inherits");
    console.log("=====================");
    console.log("");

    // Test 1: inherits is callable
    test("inherits is callable", () => {
        // Create two simple functions
        const Parent = () => {};
        const Child = () => {};
        // Call inherits - should not throw
        util.inherits(Child, Parent);
        return true;
    });

    // Note: In AOT-compiled TypeScript, class inheritance is handled
    // at compile time. util.inherits is primarily for legacy callback-style
    // code that uses function constructors. Our implementation sets
    // Child.super_ = Parent for compatibility.

    console.log("");
    console.log("Results: " + stats.passed + "/" + (stats.passed + stats.failed) + " passed");
    console.log("");
    console.log("Note: util.inherits is primarily for legacy code.");
    console.log("Modern TypeScript uses class extends syntax instead.");
    return stats.failed > 0 ? 1 : 0;
}
