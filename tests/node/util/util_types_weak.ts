// Test util.types.isWeakMap() and util.types.isWeakSet()
import * as util from 'util';

// Use object to avoid closure issues
const stats = { passed: 0, failed: 0 };

function test(name: string, fn: () => boolean): void {
    try {
        if (fn()) {
            console.log("PASS: " + name);
            stats.passed = stats.passed + 1;
        } else {
            console.log("FAIL: " + name);
            stats.failed = stats.failed + 1;
        }
    } catch (e) {
        console.log("FAIL: " + name + " (exception)");
        stats.failed = stats.failed + 1;
    }
}

function user_main(): number {
    console.log("Testing util.types.isWeakMap and isWeakSet");
    console.log("==========================================");
    console.log("");

    // Test isWeakMap
    test("isWeakMap returns true for WeakMap", () => {
        const wm = new WeakMap();
        return util.types.isWeakMap(wm);
    });

    test("isWeakMap returns false for regular Map", () => {
        const m = new Map();
        return !util.types.isWeakMap(m);
    });

    test("isWeakMap returns false for plain object", () => {
        const obj = {};
        return !util.types.isWeakMap(obj);
    });

    test("isWeakMap returns false for null", () => {
        return !util.types.isWeakMap(null);
    });

    // Test isWeakSet
    test("isWeakSet returns true for WeakSet", () => {
        const ws = new WeakSet();
        return util.types.isWeakSet(ws);
    });

    test("isWeakSet returns false for regular Set", () => {
        const s = new Set();
        return !util.types.isWeakSet(s);
    });

    test("isWeakSet returns false for plain object", () => {
        const obj = {};
        return !util.types.isWeakSet(obj);
    });

    test("isWeakSet returns false for null", () => {
        return !util.types.isWeakSet(null);
    });

    // Test that WeakMap/WeakSet still work functionally
    test("WeakMap can store and retrieve values", () => {
        const wm = new WeakMap();
        const key = {};
        wm.set(key, 42);
        return wm.get(key) === 42;
    });

    test("WeakSet can add and check values", () => {
        const ws = new WeakSet();
        const obj = {};
        ws.add(obj);
        return ws.has(obj);
    });

    console.log("");
    console.log("Results: " + stats.passed + "/" + (stats.passed + stats.failed) + " passed");
    return stats.failed > 0 ? 1 : 0;
}
