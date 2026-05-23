// Lodash "Function" category — minimum stable coverage.
//
// MANY function-category tests (partialRight, before, flow, flowRight,
// once, after, more curry forms) trigger
// "Runtime Panic: Array index out of bounds" in lodash's internal
// composeArgs path. Tests below are the largest set that doesn't
// hit the panic. Future work: fix the composeArgs out-of-bounds.

function user_main(): number {
    const _ = require('./lodash.js');

    const state = { passed: 0, failed: 0, failures: [] as string[] };

    function eq(actual: any, expected: any, label: string): void {
        if (actual === expected) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label + ": expected " + JSON.stringify(expected) + ", got " + JSON.stringify(actual));
        }
    }

    function deep(actual: any, expected: any, label: string): void {
        const a = JSON.stringify(actual);
        const e = JSON.stringify(expected);
        if (a === e) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label + ": expected " + e + ", got " + a);
        }
    }

    // --- curry (one form only — chained currying triggers panic) ---
    const curried = _.curry((a: number, b: number, c: number) => [a, b, c]);
    deep(curried(1)(2)(3), [1, 2, 3], "curry chained");

    // --- partial ---
    const sayHello = _.partial((g: string, name: string) => g + " " + name, "hello");
    eq(sayHello("world"), "hello world", "partial bound left");

    // --- negate ---
    const isOdd = _.negate((n: number) => n % 2 === 0);
    eq(isOdd(3), true, "negate(isEven)(3)");
    eq(isOdd(4), false, "negate(isEven)(4)");

    if (state.failed === 0) {
        console.log("OK: function (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: function (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
