// Lodash "Lang" extras — more type predicates and conversions
// (the second half of the Lang category that didn't fit in lang.ts).

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

    // --- More predicates ---
    eq(_.isArrayLike([1, 2, 3]), true, "isArrayLike array");
    eq(_.isArrayLike("abc"), true, "isArrayLike string");
    eq(_.isArrayLike(function () { }), false, "isArrayLike function");

    eq(_.isArrayLikeObject([1, 2, 3]), true, "isArrayLikeObject array");
    eq(_.isArrayLikeObject("abc"), false, "isArrayLikeObject string");

    eq(_.isLength(3), true, "isLength 3");
    eq(_.isLength(-1), false, "isLength -1");
    eq(_.isLength(1.5), false, "isLength 1.5");

    eq(_.isObjectLike({}), true, "isObjectLike object");
    eq(_.isObjectLike([1, 2, 3]), true, "isObjectLike array");
    eq(_.isObjectLike(function () { }), false, "isObjectLike function");
    eq(_.isObjectLike(null), false, "isObjectLike null");

    eq(_.isWeakMap(new WeakMap()), true, "isWeakMap");
    eq(_.isWeakMap({}), false, "isWeakMap neg");
    eq(_.isWeakSet(new WeakSet()), true, "isWeakSet");
    eq(_.isWeakSet({}), false, "isWeakSet neg");

    eq(_.isArguments({}), false, "isArguments object");

    // --- Comparisons (gt, gte, lt, lte) ---
    eq(_.gt(3, 1), true, "gt 3,1");
    eq(_.gt(3, 3), false, "gt 3,3");
    eq(_.gte(3, 3), true, "gte 3,3");
    eq(_.lt(1, 3), true, "lt 1,3");
    eq(_.lte(1, 1), true, "lte 1,1");

    // --- More conversions ---
    eq(_.toFinite(3.2), 3.2, "toFinite 3.2");
    eq(_.toFinite(Infinity), Number.MAX_VALUE, "toFinite Infinity");
    eq(_.toFinite("3.2"), 3.2, "toFinite '3.2'");
    eq(_.toFinite(null), 0, "toFinite null");

    eq(_.toLength(3.2), 3, "toLength 3.2");
    eq(_.toLength(-1), 0, "toLength -1");
    // lodash.toLength caps at MAX_ARRAY_LENGTH (2^32-1), not MAX_SAFE_INTEGER
    eq(_.toLength(Infinity), 4294967295, "toLength Infinity = MAX_ARRAY_LENGTH");

    eq(_.toSafeInteger(3.2), 3, "toSafeInteger 3.2");
    eq(_.toSafeInteger(Infinity), Number.MAX_SAFE_INTEGER, "toSafeInteger Infinity");

    // --- castArray ---
    eq(JSON.stringify(_.castArray(1)), "[1]", "castArray scalar");
    eq(JSON.stringify(_.castArray([1, 2])), "[1,2]", "castArray array stays array");

    // --- clone ---
    const obj = { a: 1, b: { c: 2 } };
    const cloned = _.clone(obj);
    eq(cloned.a, 1, "clone preserves shallow");
    eq(cloned === obj, false, "clone is new object");
    eq(cloned.b === obj.b, true, "clone is shallow (nested shared)");

    const deep = _.cloneDeep(obj);
    eq(deep.a, 1, "cloneDeep preserves");
    eq(deep.b === obj.b, false, "cloneDeep is deep (nested copy)");

    if (state.failed === 0) {
        console.log("OK: lang2 (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: lang2 (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
