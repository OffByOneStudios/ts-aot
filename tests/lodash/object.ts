// Lodash "Object" category — keys/values/entries, get/set/has,
// pick/omit, assign/defaults, invert, mapKeys/mapValues, merge.

function user_main(): number {
    const _ = require('./lodash.js');

    const state = { passed: 0, failed: 0, failures: [] as string[] };

    function eq(actual: any, expected: any, label: string): void {
        const a = JSON.stringify(actual);
        const e = JSON.stringify(expected);
        if (a === e) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label + ": expected " + e + ", got " + a);
        }
    }

    function bool(actual: any, expected: any, label: string): void {
        if (actual === expected) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label + ": expected " + expected + ", got " + actual);
        }
    }

    // --- keys / values / entries / toPairs ---
    eq(_.keys({ a: 1, b: 2 }), ["a", "b"], "keys");
    eq(_.values({ a: 1, b: 2 }), [1, 2], "values");
    eq(_.toPairs({ a: 1, b: 2 }), [["a", 1], ["b", 2]], "toPairs");
    eq(_.fromPairs([["a", 1], ["b", 2]]), { a: 1, b: 2 }, "fromPairs");

    // --- get / set / has / unset ---
    const obj = { a: { b: { c: 42 } } };
    bool(_.get(obj, "a.b.c"), 42, "get deep");
    bool(_.get(obj, "a.b.x"), undefined, "get missing");
    bool(_.get(obj, "a.b.x", "default"), "default", "get fallback");
    bool(_.has(obj, "a.b.c"), true, "has deep");
    bool(_.has(obj, "a.b.x"), false, "has missing");

    // --- set (deep path creation) ---
    eq(_.set({}, "x.y.z", 99), { x: { y: { z: 99 } } }, "set deep path");
    eq(_.set({}, "a.b.c.d", 7), { a: { b: { c: { d: 7 } } } }, "set 4 levels");
    const obj2 = _.set({ existing: 1 }, "x.y", 5);
    eq(obj2, { existing: 1, x: { y: 5 } }, "set with existing keys");
    eq(_.get(_.set({}, "deep.nested.path", "value"), "deep.nested.path"),
       "value", "set then get round-trip");

    // --- pick / omit / pickBy / omitBy ---
    eq(_.pick({ a: 1, b: 2, c: 3 }, ["a", "c"]), { a: 1, c: 3 }, "pick");
    eq(_.omit({ a: 1, b: 2, c: 3 }, ["b"]), { a: 1, c: 3 }, "omit");
    eq(_.pickBy({ a: 1, b: "x", c: 3 }, (v: any) => typeof v === "number"), { a: 1, c: 3 }, "pickBy");
    eq(_.omitBy({ a: 1, b: null, c: 3 }, (v: any) => v === null), { a: 1, c: 3 }, "omitBy");

    // --- assign / defaults ---
    eq(_.assign({ a: 0 }, { a: 1, b: 2 }, { c: 3 }), { a: 1, b: 2, c: 3 }, "assign overwrites");
    eq(_.defaults({ a: 1 }, { a: 2, b: 2 }), { a: 1, b: 2 }, "defaults skips existing");

    // --- invert / invertBy ---
    eq(_.invert({ a: 1, b: 2 }), { "1": "a", "2": "b" }, "invert");

    // --- mapKeys / mapValues ---
    eq(_.mapValues({ a: 1, b: 2 }, (v: number) => v * 10), { a: 10, b: 20 }, "mapValues");
    eq(_.mapKeys({ a: 1, b: 2 }, (v: number, k: string) => k + k), { aa: 1, bb: 2 }, "mapKeys");

    // --- merge (deep) ---
    eq(_.merge({ a: { b: 1 } }, { a: { c: 2 } }), { a: { b: 1, c: 2 } }, "merge deep");

    // --- findKey ---
    bool(_.findKey({ a: 1, b: 2, c: 3 }, (v: number) => v > 1), "b", "findKey");

    // --- forOwn (side-effect via shared state) ---
    const collect: string[] = [];
    _.forOwn({ a: 1, b: 2, c: 3 }, (v: number, k: string) => collect.push(k + "=" + v));
    bool(collect.length, 3, "forOwn iterates all");

    if (state.failed === 0) {
        console.log("OK: object (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: object (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
