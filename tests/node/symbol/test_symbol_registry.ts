// Test Symbol.for() and Symbol.keyFor()

function user_main(): number {
    // Test 1: Symbol.for() creates or retrieves a symbol from the global registry
    console.log("Test 1 - Symbol.for():");
    const sym1 = Symbol.for("myKey");
    const sym2 = Symbol.for("myKey");

    // Same key should return the same symbol
    if (sym1 === sym2) {
        console.log("PASS: Symbol.for() returns same symbol for same key");
    } else {
        console.log("FAIL: Symbol.for() returned different symbols");
    }

    // Test 2: Different keys return different symbols
    console.log("Test 2 - Different keys:");
    const sym3 = Symbol.for("otherKey");
    if (sym1 !== sym3) {
        console.log("PASS: Different keys return different symbols");
    } else {
        console.log("FAIL: Different keys returned same symbol");
    }

    // Test 3: Symbol.keyFor() retrieves the key for a registered symbol
    console.log("Test 3 - Symbol.keyFor():");
    const key = Symbol.keyFor(sym1);
    if (key === "myKey") {
        console.log("PASS: Symbol.keyFor() returned correct key");
    } else {
        console.log("FAIL: Symbol.keyFor() returned wrong key");
        console.log(key);
    }

    // Test 4: Symbol.keyFor() returns undefined for non-registered symbols
    console.log("Test 4 - Non-registered symbol:");
    const localSym = Symbol("local");
    const localKey = Symbol.keyFor(localSym);
    if (localKey === undefined) {
        console.log("PASS: Symbol.keyFor() returns undefined for local symbol");
    } else {
        console.log("FAIL: Symbol.keyFor() should return undefined");
    }

    return 0;
}
