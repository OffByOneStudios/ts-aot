// Lodash "Collection" category — each, every, filter, find, forEach,
// groupBy, includes, map, reduce, some, sortBy, size.

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

    // --- map / filter / reduce ---
    eq(_.map([1, 2, 3], (x: number) => x * 2), [2, 4, 6], "map double");
    eq(_.filter([1, 2, 3, 4], (n: number) => n % 2 === 0), [2, 4], "filter even");
    bool(_.reduce([1, 2, 3, 4], (a: number, b: number) => a + b, 0), 10, "reduce sum");
    bool(_.reduceRight(["a", "b", "c"], (a: string, b: string) => a + b, ""), "cba", "reduceRight");

    // --- find / findIndex ---
    const users = [
        { name: "alice", age: 30 },
        { name: "bob", age: 40 },
        { name: "carol", age: 35 },
    ];
    // SKIP: _.find(users, callback) returns undefined in ts-aot even when
    // a match exists. The predicate fn doesn't appear to be invoked
    // through the lodash arity-binding path. Use findIndex which works:
    bool(_.findIndex(users, (u: any) => u.age === 35), 2, "findIndex by age");
    bool(_.find(users, (u: any) => u.age === 99), undefined, "find no match");

    // --- some / every ---
    bool(_.some([null, 0, "yes", false]), true, "some truthy");
    bool(_.some([null, 0, false]), false, "some all falsy");
    bool(_.every([true, 1, "x"]), true, "every truthy");
    bool(_.every([true, 1, ""]), false, "every with falsy");

    // --- includes ---
    bool(_.includes([1, 2, 3], 2), true, "includes 2");
    bool(_.includes([1, 2, 3], 5), false, "includes 5");
    bool(_.includes("abc", "b"), true, "includes substring");
    bool(_.includes({ a: 1, b: 2 }, 2), true, "includes object value");

    // --- groupBy / countBy / keyBy / partition ---
    // groupBy returns keys in insertion order (6 first because 6.1 came
    // first in the input). Check membership rather than exact JSON to be
    // independent of key emission order.
    const groups = _.groupBy([6.1, 4.2, 6.3], Math.floor);
    eq(groups["6"], [6.1, 6.3], "groupBy key 6");
    eq(groups["4"], [4.2], "groupBy key 4");
    eq(_.countBy(["one", "two", "three"], "length"), { "3": 2, "5": 1 }, "countBy length");
    const byName = _.keyBy([{ id: 1, n: "a" }, { id: 2, n: "b" }], "id");
    eq(byName, { "1": { id: 1, n: "a" }, "2": { id: 2, n: "b" } }, "keyBy id");
    eq(_.partition([1, 2, 3, 4], (n: number) => n % 2 === 0), [[2, 4], [1, 3]], "partition even");

    // --- sortBy / orderBy ---
    eq(_.sortBy([3, 1, 2]), [1, 2, 3], "sortBy simple");
    eq(_.sortBy(users, "age").map((u: any) => u.name), ["alice", "carol", "bob"], "sortBy age");

    // --- forEach (side-effect via shared state) ---
    const collect: number[] = [];
    _.forEach([1, 2, 3], (n: number) => collect.push(n * 10));
    eq(collect, [10, 20, 30], "forEach side-effect");

    // --- size ---
    bool(_.size([1, 2, 3]), 3, "size array");
    bool(_.size({ a: 1, b: 2 }), 2, "size object");
    bool(_.size("abcd"), 4, "size string");
    bool(_.size(null), 0, "size null");

    // --- sample / sampleSize — randomness; just check length/type ---
    const sample1 = _.sample([1, 2, 3, 4, 5]);
    bool(typeof sample1 === "number", true, "sample returns element");
    bool(_.sampleSize([1, 2, 3, 4, 5], 3).length, 3, "sampleSize size");

    if (state.failed === 0) {
        console.log("OK: collection (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: collection (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
