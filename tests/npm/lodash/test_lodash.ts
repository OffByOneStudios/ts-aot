// Test: lodash - modular utility functions
// Exercises: CommonJS require chains, type checking, value utilities
// Uses sub-path imports (lodash/identity) to avoid monolithic runInContext() initialization

import identity from 'lodash/identity';
import compact from 'lodash/compact';
import head from 'lodash/head';
import last from 'lodash/last';
import eq from 'lodash/eq';
import defaultTo from 'lodash/defaultTo';
import isNil from 'lodash/isNil';
import isNull from 'lodash/isNull';
import isUndefined from 'lodash/isUndefined';
import isObject from 'lodash/isObject';
import isObjectLike from 'lodash/isObjectLike';
import noop from 'lodash/noop';
import stubTrue from 'lodash/stubTrue';
import stubFalse from 'lodash/stubFalse';
import stubArray from 'lodash/stubArray';
import stubObject from 'lodash/stubObject';
import stubString from 'lodash/stubString';
import constant from 'lodash/constant';
// isArray/flatten/negate have deeper dependency chains (Array.isArray property access,
// baseFlatten, typeof function checks) that need more runtime support

function user_main(): number {
    let failures = 0;

    // --- identity ---
    if (identity(42) !== 42) { console.log("FAIL: identity(42)"); failures++; }
    if (identity("hello") !== "hello") { console.log("FAIL: identity('hello')"); failures++; }
    if (identity(null) !== null) { console.log("FAIL: identity(null)"); failures++; }
    if (identity(undefined) !== undefined) { console.log("FAIL: identity(undefined)"); failures++; }
    console.log("identity: 4 checks");

    // --- eq (SameValueZero comparison) ---
    if (eq(1, 1) !== true) { console.log("FAIL: eq(1,1)"); failures++; }
    if (eq("a", "a") !== true) { console.log("FAIL: eq('a','a')"); failures++; }
    if (eq(1, 2) !== false) { console.log("FAIL: eq(1,2)"); failures++; }
    if (eq(null, undefined) !== false) { console.log("FAIL: eq(null,undefined)"); failures++; }
    if (eq(null, null) !== true) { console.log("FAIL: eq(null,null)"); failures++; }
    if (eq(undefined, undefined) !== true) { console.log("FAIL: eq(undef,undef)"); failures++; }
    console.log("eq: 6 checks");

    // --- defaultTo ---
    if (defaultTo(1, 10) !== 1) { console.log("FAIL: defaultTo(1,10)"); failures++; }
    if (defaultTo(undefined, 10) !== 10) { console.log("FAIL: defaultTo(undefined,10)"); failures++; }
    if (defaultTo(null, 10) !== 10) { console.log("FAIL: defaultTo(null,10)"); failures++; }
    console.log("defaultTo: 3 checks");

    // --- compact ---
    const compacted: any[] = compact([0, 1, false, 2, "", 3]);
    if (compacted.length !== 3) { console.log("FAIL: compact length"); failures++; }
    if (compacted[0] !== 1) { console.log("FAIL: compact[0]"); failures++; }
    if (compacted[2] !== 3) { console.log("FAIL: compact[2]"); failures++; }
    console.log("compact: 3 checks");

    // --- head / last ---
    if (head([10, 20, 30]) !== 10) { console.log("FAIL: head"); failures++; }
    if (head([]) !== undefined) { console.log("FAIL: head([])"); failures++; }
    if (last([10, 20, 30]) !== 30) { console.log("FAIL: last"); failures++; }
    if (last([]) !== undefined) { console.log("FAIL: last([])"); failures++; }
    console.log("head/last: 4 checks");

    // --- isNil / isNull / isUndefined ---
    if (isNil(null) !== true) { console.log("FAIL: isNil(null)"); failures++; }
    if (isNil(undefined) !== true) { console.log("FAIL: isNil(undefined)"); failures++; }
    if (isNil(0) !== false) { console.log("FAIL: isNil(0)"); failures++; }
    if (isNull(null) !== true) { console.log("FAIL: isNull(null)"); failures++; }
    if (isNull(undefined) !== false) { console.log("FAIL: isNull(undefined)"); failures++; }
    if (isUndefined(undefined) !== true) { console.log("FAIL: isUndefined(undefined)"); failures++; }
    if (isUndefined(null) !== false) { console.log("FAIL: isUndefined(null)"); failures++; }
    console.log("isNil/isNull/isUndefined: 7 checks");

    // --- isObject / isObjectLike ---
    if (isObject(null) !== false) { console.log("FAIL: isObject(null)"); failures++; }
    if (isObject(1) !== false) { console.log("FAIL: isObject(1)"); failures++; }
    if (isObjectLike(null) !== false) { console.log("FAIL: isObjectLike(null)"); failures++; }
    console.log("isObject/isObjectLike: 3 checks");

    // --- stubs ---
    if (noop() !== undefined) { console.log("FAIL: noop()"); failures++; }
    if (stubTrue() !== true) { console.log("FAIL: stubTrue()"); failures++; }
    if (stubFalse() !== false) { console.log("FAIL: stubFalse()"); failures++; }
    if (stubString() !== "") { console.log("FAIL: stubString()"); failures++; }
    const arr: any[] = stubArray();
    if (arr.length !== 0) { console.log("FAIL: stubArray()"); failures++; }
    const obj: any = stubObject();
    if (typeof obj !== "object") { console.log("FAIL: stubObject()"); failures++; }
    console.log("stubs: 6 checks");

    // --- constant ---
    const alwaysFive = constant(5);
    if (alwaysFive() !== 5) { console.log("FAIL: constant(5)()"); failures++; }
    if (alwaysFive() !== 5) { console.log("FAIL: constant(5)() again"); failures++; }
    console.log("constant: 2 checks");

    // --- Summary ---
    console.log("---");
    const total = 38;
    const passed = total - failures;
    console.log(passed + "/" + total + " lodash tests passed");
    if (failures > 0) {
        console.log(failures + " test(s) failed");
    } else {
        console.log("All lodash tests passed!");
    }
    process.exit(failures);
    return failures;
}
