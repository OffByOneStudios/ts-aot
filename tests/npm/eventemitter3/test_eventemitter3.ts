// Test: eventemitter3 - lightweight event emitter
// Exercises: prototype methods, this binding, closures, constructor functions

import EventEmitter from 'eventemitter3';

function user_main(): number {
    let failures = 0;

    // Test 1: on() + emit() fires callback
    const ee1 = new EventEmitter();
    let called = false;
    ee1.on("test", () => {
        called = true;
    });
    ee1.emit("test");
    if (called === true) {
        console.log("PASS: on() + emit() fires callback");
    } else {
        console.log("FAIL: on() + emit() callback not called");
        failures++;
    }

    // Test 2: emit with argument
    const ee2 = new EventEmitter();
    let received = "";
    ee2.on("data", (msg: string) => {
        received = msg;
    });
    ee2.emit("data", "hello");
    if (received === "hello") {
        console.log("PASS: emit passes argument to listener");
    } else {
        console.log("FAIL: emit argument expected 'hello', got '" + received + "'");
        failures++;
    }

    // Test 3: once() fires only once
    const ee3 = new EventEmitter();
    let onceCount = 0;
    ee3.once("ping", () => {
        onceCount++;
    });
    ee3.emit("ping");
    ee3.emit("ping");
    if (onceCount === 1) {
        console.log("PASS: once() fires only once");
    } else {
        console.log("FAIL: once() expected 1 call, got " + String(onceCount));
        failures++;
    }

    // Test 4: emit returns true when listeners exist
    const ee4 = new EventEmitter();
    ee4.on("exists", () => {});
    const hasListeners = ee4.emit("exists");
    if (hasListeners === true) {
        console.log("PASS: emit returns true when listeners exist");
    } else {
        console.log("FAIL: emit expected true, got " + String(hasListeners));
        failures++;
    }

    // Test 5: emit returns false when no listeners
    const ee5 = new EventEmitter();
    const noListeners = ee5.emit("nope");
    if (noListeners === false) {
        console.log("PASS: emit returns false when no listeners");
    } else {
        console.log("FAIL: emit expected false, got " + String(noListeners));
        failures++;
    }

    // Test 6: removeListener removes specific listener
    const ee6 = new EventEmitter();
    let removeCount = 0;
    const handler = () => { removeCount++; };
    ee6.on("ev", handler);
    ee6.emit("ev");
    ee6.removeListener("ev", handler);
    ee6.emit("ev");
    if (removeCount === 1) {
        console.log("PASS: removeListener removes specific listener");
    } else {
        console.log("FAIL: removeListener expected count 1, got " + String(removeCount));
        failures++;
    }

    // Test 7: listenerCount returns correct count
    const ee7 = new EventEmitter();
    ee7.on("a", () => {});
    ee7.on("a", () => {});
    const count = ee7.listenerCount("a");
    if (count === 2) {
        console.log("PASS: listenerCount returns 2");
    } else {
        console.log("FAIL: listenerCount expected 2, got " + String(count));
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All eventemitter3 tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    process.exit(failures);
    return failures;
}
