// Test globalThis (ES2020)

function user_main(): number {
    console.log("Testing globalThis...");

    // Test 1: globalThis exists and is an object
    console.log("typeof globalThis = " + typeof globalThis);

    // Test 2: globalThis === global (Node.js)
    console.log("globalThis === global: " + (globalThis === global));

    // Test 3: globalThis.globalThis === globalThis (self-reference)
    console.log("globalThis.globalThis === globalThis: " + (globalThis.globalThis === globalThis));

    // Test 4: Access built-ins through globalThis
    console.log("typeof globalThis.console = " + typeof globalThis.console);
    console.log("typeof globalThis.Object = " + typeof globalThis.Object);
    console.log("typeof globalThis.Array = " + typeof globalThis.Array);

    // Test 5: globalThis has Math
    console.log("globalThis.Math.PI exists: " + (globalThis.Math !== undefined));

    console.log("All globalThis tests passed!");
    return 0;
}
