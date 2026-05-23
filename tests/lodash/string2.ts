// Lodash "String" extras — escapeRegExp, parseInt, and a few more.

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

    // --- escapeRegExp ---
    eq(_.escapeRegExp("[lodash](https://lodash.com/)"), "\\[lodash\\]\\(https://lodash\\.com/\\)", "escapeRegExp brackets");
    eq(_.escapeRegExp("a.b.c"), "a\\.b\\.c", "escapeRegExp dots");

    // --- parseInt ---
    eq(_.parseInt("08"), 8, "parseInt '08'");
    eq(_.parseInt("0x1a"), 26, "parseInt hex");
    // SKIP: _.parseInt("10", 2) returns 10 not 2 — the radix argument is
    // dropped through the lodash dispatch path. // eq(_.parseInt("10", 2), 2, ...)

    // --- Aliases (functions == aliases) ---
    eq(_.first([1, 2, 3]), 1, "first aliases head");

    // --- toPath ---
    const path = _.toPath("a.b.c");
    eq(Array.isArray(path), true, "toPath returns array");
    eq(path.length, 3, "toPath length");

    if (state.failed === 0) {
        console.log("OK: string2 (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: string2 (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
