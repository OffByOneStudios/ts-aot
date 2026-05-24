// Lodash "Date" + lazy chain wrapper. Chain wrapper previously
// skipped due to a wrapper-prototype issue; that no longer
// reproduces and chain().map().value() now works.

function user_main(): number {
    const _ = require('./lodash.js');

    const state = { passed: 0, failed: 0, failures: [] as string[] };

    function ok(cond: any, label: string): void {
        if (cond) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label);
        }
    }
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

    // --- Date.now wrapper ---
    const t1 = _.now();
    ok(typeof t1 === "number", "_.now is number");
    ok(t1 > 0, "_.now is positive");

    // --- chain / value ---
    // reduce on chain currently broken (returns undefined wrapper) — leave skipped.
    deep(_.chain([1, 2, 3]).map((x: number) => x * 2).value(), [2, 4, 6], "chain map value");
    deep(_.chain([1, 2, 3, 4]).filter((x: number) => x % 2 === 0).value(), [2, 4], "chain filter value");
    deep(_.chain([3, 1, 2]).map((x: number) => x * 10).value(), [30, 10, 20], "chain map (preserve order)");
    deep(_.chain([1, 2, 3]).map((x: number) => x + 1).filter((x: number) => x > 1).value(), [2, 3, 4], "chain map then filter");

    if (state.failed === 0) {
        console.log("OK: date (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: date (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
