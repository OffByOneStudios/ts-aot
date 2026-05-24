// Lodash "Util" extras: over, overEvery, overSome, cond, method,
// conforms, rangeRight, nthArg. Previously marked SKIP due to a
// composeArgs OOB panic; that root cause was closed by the
// var-hoisting + isFinite-shadow fixes in 2026-05-23.

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

    // --- over (applies multiple iteratees, returns array of results) ---
    deep(_.over(Math.max, Math.min)(1, 2, 3, 4), [4, 1], "over max/min");
    deep(_.over((x: number) => x * 2, (x: number) => x + 1)(5), [10, 6], "over fns");

    // --- overEvery (all iteratees return truthy) ---
    eq(_.overEvery((x: number) => x > 1, (x: number) => x < 10)(5), true, "overEvery in range");
    eq(_.overEvery((x: number) => x > 1, (x: number) => x < 10)(15), false, "overEvery out of range");
    eq(_.overEvery((x: number) => x > 0)(1), true, "overEvery single iteratee");

    // --- overSome (any iteratee returns truthy) ---
    eq(_.overSome((x: number) => x > 100, (x: number) => x < 0)(5), false, "overSome neither matches");
    eq(_.overSome((x: number) => x > 100, (x: number) => x < 0)(-1), true, "overSome second matches");
    eq(_.overSome((x: number) => x > 100, (x: number) => x < 0)(200), true, "overSome first matches");

    // --- cond (pred/transform pairs) ---
    const grade = _.cond([
        [(n: number) => n >= 90, () => "A"],
        [(n: number) => n >= 80, () => "B"],
        [() => true, () => "F"],
    ]);
    eq(grade(95), "A", "cond A");
    eq(grade(85), "B", "cond B");
    eq(grade(50), "F", "cond fallback");

    // --- method (invokes named method on argument) ---
    eq(_.method("toUpperCase")("hi"), "HI", "method toUpperCase");
    eq(_.method("slice", 1)("hello"), "ello", "method slice 1");

    // --- conforms (predicate spec on object) ---
    eq(_.conforms({ a: (n: number) => n > 0 })({ a: 5 }), true, "conforms pass");
    eq(_.conforms({ a: (n: number) => n > 0 })({ a: -1 }), false, "conforms fail");

    // --- rangeRight ---
    deep(_.rangeRight(4), [3, 2, 1, 0], "rangeRight n");
    deep(_.rangeRight(1, 5), [4, 3, 2, 1], "rangeRight start end");
    deep(_.rangeRight(0, 10, 2), [8, 6, 4, 2, 0], "rangeRight step");

    // --- nthArg (returns the Nth argument passed to the resulting fn) ---
    eq(_.nthArg(1)("a", "b", "c"), "b", "nthArg 1");
    eq(_.nthArg(0)("a", "b", "c"), "a", "nthArg 0");
    eq(_.nthArg(-1)("a", "b", "c"), "c", "nthArg negative");

    if (state.failed === 0) {
        console.log("OK: util2 (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: util2 (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
