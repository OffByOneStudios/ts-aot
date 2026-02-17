// Test importing and using a class from another module

import { Counter } from './cross_module_class_lib';

function user_main(): number {
    let failures = 0;

    // Test 1: Basic construction and method calls
    const c = new Counter(0);
    c.increment();
    if (c.getCount() === 1) {
        console.log("PASS: Counter increment works cross-module");
    } else {
        console.log("FAIL: expected 1, got " + c.getCount());
        failures++;
    }

    // Test 2: Constructor with non-zero initial value
    const c2 = new Counter(10);
    if (c2.getCount() === 10) {
        console.log("PASS: Counter constructor with initial value");
    } else {
        console.log("FAIL: expected 10, got " + c2.getCount());
        failures++;
    }

    // Test 3: Static method works cross-module
    const c3 = Counter.create();
    if (c3.getCount() === 0) {
        console.log("PASS: Counter.create() static method works");
    } else {
        console.log("FAIL: expected 0, got " + c3.getCount());
        failures++;
    }

    // Test 4: Multiple instances are independent
    const a = new Counter(5);
    const b = new Counter(10);
    a.increment();
    b.decrement();
    if (a.getCount() === 6 && b.getCount() === 9) {
        console.log("PASS: multiple instances are independent");
    } else {
        console.log("FAIL: expected 6 and 9, got " + a.getCount() + " and " + b.getCount());
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All cross-module class tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
