// Lodash "Math" + "Number" categories — add, ceil, divide, floor,
// max/min/mean/sum, round, subtract, multiply, clamp, inRange.

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

    function close(actual: number, expected: number, label: string): void {
        if (Math.abs(actual - expected) < 1e-9) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label + ": expected ~" + expected + ", got " + actual);
        }
    }

    // --- Arithmetic ---
    eq(_.add(6, 4), 10, "add");
    eq(_.subtract(6, 4), 2, "subtract");
    eq(_.multiply(6, 4), 24, "multiply");
    eq(_.divide(6, 4), 1.5, "divide");

    // --- Aggregates ---
    eq(_.sum([4, 2, 8, 6]), 20, "sum");
    eq(_.sum([]), 0, "sum empty");
    eq(_.sumBy([{ n: 4 }, { n: 2 }, { n: 8 }], "n"), 14, "sumBy property");
    eq(_.sumBy([{ n: 4 }, { n: 2 }, { n: 8 }], (o: any) => o.n), 14, "sumBy fn");
    eq(_.mean([4, 2, 8, 6]), 5, "mean");
    close(_.meanBy([{ n: 4 }, { n: 2 }, { n: 8 }], "n"), 14 / 3, "meanBy");

    eq(_.max([4, 2, 8, 6]), 8, "max");
    eq(_.min([4, 2, 8, 6]), 2, "min");
    eq(_.max([]), undefined, "max empty");
    eq(_.min([]), undefined, "min empty");
    eq(_.maxBy([{ n: 1 }, { n: 2 }], "n").n, 2, "maxBy");
    eq(_.minBy([{ n: 1 }, { n: 2 }], "n").n, 1, "minBy");

    // --- Rounding ---
    eq(_.ceil(4.006), 5, "ceil");
    eq(_.ceil(6.004, 2), 6.01, "ceil precision");
    eq(_.floor(4.906), 4, "floor");
    eq(_.floor(0.046, 2), 0.04, "floor precision");
    eq(_.round(4.006), 4, "round");
    eq(_.round(4.7), 5, "round 4.7");
    eq(_.round(4.006, 2), 4.01, "round precision");

    // --- Number utilities ---
    eq(_.clamp(-10, -5, 5), -5, "clamp lower");
    eq(_.clamp(10, -5, 5), 5, "clamp upper");
    eq(_.clamp(0, -5, 5), 0, "clamp inside");
    eq(_.inRange(3, 2, 4), true, "inRange yes");
    eq(_.inRange(4, 8), true, "inRange 0..8");
    eq(_.inRange(4, 2), false, "inRange beyond");

    if (state.failed === 0) {
        console.log("OK: math (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: math (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
