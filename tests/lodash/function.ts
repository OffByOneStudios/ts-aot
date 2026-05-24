// Lodash "Function" category — expanded after the composeArgs OOB
// root cause was closed by 2026-05-23 fixes (var-hoisting recursion +
// isFinite shadow exclusion). Memoize still skipped (MapCache.has
// always-false — separate prototype-linkage bug).

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

    // --- curry ---
    const curried = _.curry((a: number, b: number, c: number) => [a, b, c]);
    deep(curried(1)(2)(3), [1, 2, 3], "curry chained");
    deep(curried(1, 2, 3), [1, 2, 3], "curry full apply");
    deep(curried(1, 2)(3), [1, 2, 3], "curry split 2+1");
    deep(curried(1)(2, 3), [1, 2, 3], "curry split 1+2");

    // --- partial / partialRight ---
    const sayHello = _.partial((g: string, name: string) => g + " " + name, "hello");
    eq(sayHello("world"), "hello world", "partial bound left");

    const greet = _.partialRight((g: string, name: string) => g + " " + name, "world");
    eq(greet("hello"), "hello world", "partialRight bound right");

    // --- negate ---
    const isOdd = _.negate((n: number) => n % 2 === 0);
    eq(isOdd(3), true, "negate(isEven)(3)");
    eq(isOdd(4), false, "negate(isEven)(4)");

    // --- before (calls func only first n-1 times; subsequent calls return last value) ---
    var beforeCalls: number = 0;
    const lim = _.before(3, () => { beforeCalls++; return beforeCalls * 10; });
    eq(lim(), 10, "before call 1");
    eq(lim(), 20, "before call 2");
    eq(lim(), 20, "before call 3 (capped)");
    eq(lim(), 20, "before call 4 (capped)");
    eq(beforeCalls, 2, "before total invocations");

    // --- after (calls func only after n calls) ---
    var afterCalls: number = 0;
    const trigger = _.after(2, () => { afterCalls++; return "done"; });
    eq(trigger(), undefined, "after call 1 (skipped)");
    eq(trigger(), "done", "after call 2 (fired)");
    eq(trigger(), "done", "after call 3 (fired)");
    eq(afterCalls, 2, "after total invocations");

    // --- once (call only the first time) ---
    var onceCalls: number = 0;
    const initOnce = _.once(() => { onceCalls++; return 42; });
    eq(initOnce(), 42, "once first call");
    eq(initOnce(), 42, "once second call (cached)");
    eq(initOnce(), 42, "once third call (cached)");
    eq(onceCalls, 1, "once invoked once");

    // --- flow / flowRight (function composition) ---
    const addOne = (n: number) => n + 1;
    const double = (n: number) => n * 2;
    eq(_.flow([addOne, double])(3), 8, "flow left-to-right (3+1)*2");
    eq(_.flowRight([addOne, double])(3), 7, "flowRight right-to-left (3*2)+1");

    if (state.failed === 0) {
        console.log("OK: function (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: function (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
