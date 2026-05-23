// Lodash "Util" category — identity, noop, constant, times, range,
// uniqueId, defaultTo, stubArray/stubFalse/etc, attempt.

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

    // --- identity ---
    eq(_.identity(42), 42, "identity 42");
    eq(_.identity("x"), "x", "identity 'x'");

    // --- constant ---
    eq(_.constant(7)(), 7, "constant");
    eq(_.constant("hi")(), "hi", "constant string");

    // --- noop ---
    eq(_.noop(), undefined, "noop returns undefined");
    eq(_.noop(1, 2, 3), undefined, "noop ignores args");

    // --- defaultTo ---
    eq(_.defaultTo(undefined, 10), 10, "defaultTo undefined");
    eq(_.defaultTo(null, 10), 10, "defaultTo null");
    eq(_.defaultTo(NaN, 10), 10, "defaultTo NaN");
    eq(_.defaultTo(0, 10), 0, "defaultTo 0 (not nil)");
    eq(_.defaultTo("a", "b"), "a", "defaultTo string");

    // --- times ---
    deep(_.times(3, (i: number) => i * 2), [0, 2, 4], "times mapper");
    deep(_.times(0, (i: number) => i), [], "times 0");

    // --- range ---
    deep(_.range(4), [0, 1, 2, 3], "range(4)");
    deep(_.range(1, 5), [1, 2, 3, 4], "range(1,5)");
    deep(_.range(0, 20, 5), [0, 5, 10, 15], "range step");
    deep(_.range(0, -4, -1), [0, -1, -2, -3], "range neg");

    // --- stubFalse / stubTrue / stubArray / stubObject / stubString ---
    eq(_.stubFalse(), false, "stubFalse");
    eq(_.stubTrue(), true, "stubTrue");
    deep(_.stubArray(), [], "stubArray");
    deep(_.stubObject(), {}, "stubObject");
    eq(_.stubString(), "", "stubString");

    // --- uniqueId ---
    const a = _.uniqueId();
    const b = _.uniqueId();
    eq(a !== b, true, "uniqueId distinct");
    const c = _.uniqueId("prefix_");
    eq(c.indexOf("prefix_") === 0, true, "uniqueId prefix");

    // --- attempt ---
    eq(_.attempt(() => "ok"), "ok", "attempt no-throw");
    // Errors from attempt should be returned as an Error instance:
    const err = _.attempt(() => { throw new Error("oops"); });
    eq(_.isError(err), true, "attempt returns Error");

    // --- iteratee shorthand (string -> property accessor) ---
    const userList = [{ name: "alice", age: 30 }, { name: "bob", age: 40 }];
    const nameFn = _.iteratee("name");
    eq(nameFn(userList[0]), "alice", "iteratee('name')");

    // --- matches / matchesProperty ---
    const matchAlice = _.matches({ name: "alice" });
    eq(matchAlice(userList[0]), true, "matches positive");
    eq(matchAlice(userList[1]), false, "matches negative");

    const isAge30 = _.matchesProperty("age", 30);
    eq(isAge30(userList[0]), true, "matchesProperty positive");
    eq(isAge30(userList[1]), false, "matchesProperty negative");

    if (state.failed === 0) {
        console.log("OK: util (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: util (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
