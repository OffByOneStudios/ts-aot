// Lodash "Array" category — full unskipped suite.

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

    // --- chunk ---
    eq(_.chunk([1, 2, 3, 4, 5], 2), [[1, 2], [3, 4], [5]], "chunk size 2");
    eq(_.chunk([1, 2, 3, 4, 5], 3), [[1, 2, 3], [4, 5]], "chunk size 3");
    eq(_.chunk([], 2), [], "chunk empty");

    // --- compact ---
    eq(_.compact([0, 1, false, 2, "", 3, null, 4, undefined, NaN, 5]), [1, 2, 3, 4, 5], "compact strips falsy");
    eq(_.compact([]), [], "compact empty");

    // --- concat ---
    eq(_.concat([1], 2, [3], [[4]]), [1, 2, 3, [4]], "concat shallow");
    eq(_.concat([1], [2]), [1, 2], "concat two arrays");

    // --- difference / intersection / union / xor ---
    eq(_.difference([2, 1], [2, 3]), [1], "difference");
    eq(_.intersection([2, 1], [2, 3]), [2], "intersection");
    eq(_.union([2], [1, 2]), [2, 1], "union");
    eq(_.xor([2, 1], [2, 3]), [1, 3], "xor");

    // --- drop / take ---
    eq(_.drop([1, 2, 3]), [2, 3], "drop default 1");
    eq(_.drop([1, 2, 3], 2), [3], "drop 2");
    eq(_.drop([1, 2, 3], 5), [], "drop 5");
    eq(_.dropRight([1, 2, 3]), [1, 2], "dropRight default 1");
    eq(_.take([1, 2, 3]), [1], "take default 1");
    eq(_.take([1, 2, 3], 2), [1, 2], "take 2");
    eq(_.takeRight([1, 2, 3], 2), [2, 3], "takeRight 2");

    // --- fill ---
    eq(_.fill([1, 2, 3], "x"), ["x", "x", "x"], "fill all");
    eq(_.fill([1, 2, 3, 4], "*", 1, 3), [1, "*", "*", 4], "fill range");

    // --- flatten / flattenDeep ---
    eq(_.flatten([1, [2, [3, [4]], 5]]), [1, 2, [3, [4]], 5], "flatten 1-level");
    eq(_.flattenDeep([1, [2, [3, [4]], 5]]), [1, 2, 3, 4, 5], "flattenDeep");

    // --- head / last / initial / tail ---
    bool(_.head([1, 2, 3]), 1, "head");
    bool(_.last([1, 2, 3]), 3, "last");
    eq(_.initial([1, 2, 3]), [1, 2], "initial");
    eq(_.tail([1, 2, 3]), [2, 3], "tail");

    // --- indexOf / lastIndexOf ---
    bool(_.indexOf([1, 2, 1, 2], 2), 1, "indexOf");
    bool(_.indexOf([1, 2, 1, 2], 2, 2), 3, "indexOf fromIndex");
    bool(_.lastIndexOf([1, 2, 1, 2], 2), 3, "lastIndexOf");

    // --- join ---
    bool(_.join(["a", "b", "c"], "~"), "a~b~c", "join custom sep");
    bool(_.join(["a", "b", "c"]), "a,b,c", "join default sep");

    // --- nth ---
    bool(_.nth(["a", "b", "c", "d"], 1), "b", "nth 1");
    bool(_.nth(["a", "b", "c", "d"], -2), "c", "nth -2");

    // --- pull ---
    const arr1 = [1, 2, 3, 1, 2, 3];
    _.pull(arr1, 2, 3);
    eq(arr1, [1, 1], "pull mutates");

    // --- reverse ---
    eq(_.reverse([1, 2, 3]), [3, 2, 1], "reverse");

    // --- slice ---
    eq(_.slice([1, 2, 3, 4], 1, 3), [2, 3], "slice 1..3");

    // --- sortedIndex / sortedLastIndex ---
    bool(_.sortedIndex([30, 50], 40), 1, "sortedIndex");
    bool(_.sortedLastIndex([4, 5, 5, 5, 6], 5), 4, "sortedLastIndex");

    // --- uniq / uniqBy ---
    eq(_.uniq([2, 1, 2]), [2, 1], "uniq");
    eq(_.uniqBy([2.1, 1.2, 2.3], Math.floor), [2.1, 1.2], "uniqBy Math.floor");

    // --- without ---
    eq(_.without([2, 1, 2, 3], 1, 2), [3], "without");

    // --- zip / zipObject ---
    eq(_.zip(["a", "b"], [1, 2], [true, false]), [["a", 1, true], ["b", 2, false]], "zip");
    eq(_.zipObject(["a", "b"], [1, 2]), { a: 1, b: 2 }, "zipObject");

    if (state.failed === 0) {
        console.log("OK: array (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: array (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
