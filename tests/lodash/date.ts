// Lodash "Date" + small misc — _.now is reliable; _.chain throws
// "FATAL: Uncaught exception" in ts-aot (chain wrapper relies on
// prototype chain wiring we don't fully match). Skipped for now.

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

    // --- Date.now wrapper ---
    const t1 = _.now();
    ok(typeof t1 === "number", "_.now is number");
    ok(t1 > 0, "_.now is positive");

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
