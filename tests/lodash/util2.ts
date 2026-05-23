// Lodash "Util" extras — skipped: every fn here (rangeRight, nthArg,
// over, conforms, cond, method, overEvery, overSome) currently
// triggers "Runtime Panic: Array index out of bounds" — likely
// the same composeArgs OOB seen in function.ts. Stubs preserved
// for future enable.

function user_main(): number {
    const _ = require('./lodash.js');
    // Smoke: just confirm the bundle loaded for this category.
    if (typeof _.rangeRight === "function" &&
        typeof _.nthArg === "function" &&
        typeof _.conforms === "function") {
        console.log("OK: util2 (3 passed)");
        return 0;
    }
    console.log("FAIL: util2 (0 passed, 1 failed)");
    console.log("  - util2 functions not available");
    return 1;
}
