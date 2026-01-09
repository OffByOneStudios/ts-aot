// Test Array.prototype.entries(), keys(), values()

function user_main(): number {
    const arr: string[] = ["a", "b", "c"];

    // Test 1: keys()
    console.log("Test 1 - keys():");
    const keys = arr.keys();
    console.log(keys.length);  // Should print 3
    for (const k of keys) {
        console.log(k);  // Should print 0, 1, 2
    }

    // Test 2: values()
    console.log("Test 2 - values():");
    const values = arr.values();
    console.log(values.length);  // Should print 3
    for (const v of values) {
        console.log(v);  // Should print a, b, c
    }

    // Test 3: entries - check the length of the outer array
    console.log("Test 3 - entries():");
    const entries = arr.entries();
    console.log(entries.length);  // Should print 3

    return 0;
}
