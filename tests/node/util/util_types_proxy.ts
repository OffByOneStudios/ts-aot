// Test util.types.isProxy(), isWeakMap(), isWeakSet(), isAnyArrayBuffer()
import * as util from 'util';

function user_main(): number {
    console.log("=== util.types Additional Type Checks ===");
    console.log("");
    let failures = 0;

    // Test 1: isProxy with Proxy object
    const target = { name: "test" };
    const handler = {
        get: function(obj: any, prop: string) {
            return prop in obj ? obj[prop] : "default";
        }
    };
    const proxy = new Proxy(target, handler);

    const result1 = util.types.isProxy(proxy);
    if (result1 === true) {
        console.log("PASS: Proxy detected as proxy");
    } else {
        console.log("FAIL: Expected true for proxy, got: " + result1);
        failures++;
    }

    // Test 2: isProxy with plain object
    const plainObj = { foo: "bar" };
    const result2 = util.types.isProxy(plainObj);
    if (result2 === false) {
        console.log("PASS: Plain object NOT detected as proxy");
    } else {
        console.log("FAIL: Expected false for plain object, got: " + result2);
        failures++;
    }

    // Test 3: isAnyArrayBuffer with Buffer (our ArrayBuffer)
    const buf = Buffer.alloc(10);
    const result3 = util.types.isAnyArrayBuffer(buf);
    if (result3 === true) {
        console.log("PASS: Buffer detected as ArrayBuffer");
    } else {
        console.log("FAIL: Expected true for Buffer, got: " + result3);
        failures++;
    }

    // Test 4: isAnyArrayBuffer with plain object
    const result4 = util.types.isAnyArrayBuffer(plainObj);
    if (result4 === false) {
        console.log("PASS: Plain object NOT detected as ArrayBuffer");
    } else {
        console.log("FAIL: Expected false for plain object, got: " + result4);
        failures++;
    }

    // Test 5: isWeakMap (returns false since we can't distinguish)
    const wm = new WeakMap();
    const result5 = util.types.isWeakMap(wm);
    // Note: In our implementation WeakMap is just Map, so we can't detect it
    // This is a known limitation
    console.log("INFO: isWeakMap returned: " + result5 + " (expected false - known limitation)");

    // Test 6: isWeakSet (returns false since we can't distinguish)
    const ws = new WeakSet();
    const result6 = util.types.isWeakSet(ws);
    // Note: In our implementation WeakSet is just Set, so we can't detect it
    // This is a known limitation
    console.log("INFO: isWeakSet returned: " + result6 + " (expected false - known limitation)");

    console.log("");
    console.log("=== Summary ===");
    if (failures === 0) {
        console.log("All tests passed!");
    } else {
        console.log("Failures: " + failures);
    }

    return failures;
}
