// Cross-module direct calls with NUMERIC and BOOLEAN args. Regression test
// for the silent weak-stub breakage: imported function calls compiled to a
// wrong mangled name (caller module hash + arg-inferred types), linked
// against a weak stub returning undefined, and produced NaN instead of a
// link error.

import { sumSquares, scale, pick } from './cross_module_typed_lib';

function user_main(): number {
    let failures = 0;

    const s = sumSquares(100);
    if (s === 328350) {
        console.log("PASS: sumSquares(100) === 328350");
    } else {
        console.log("FAIL: sumSquares(100) expected 328350, got " + s);
        failures++;
    }

    const sc = scale(21, 2);
    if (sc === 42) {
        console.log("PASS: scale(21, 2) === 42");
    } else {
        console.log("FAIL: scale(21, 2) expected 42, got " + sc);
        failures++;
    }

    const p1 = pick(true, 7, 9);
    const p2 = pick(false, 7, 9);
    if (p1 === 7 && p2 === 9) {
        console.log("PASS: pick() boolean+numeric args");
    } else {
        console.log("FAIL: pick() expected 7/9, got " + p1 + "/" + p2);
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All cross-module typed tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }
    return failures;
}
