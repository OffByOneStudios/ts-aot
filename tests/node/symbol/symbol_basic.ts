// Test Symbol basic functionality (ES2015)

function user_main(): number {
    console.log("=== Symbol Basic Test ===");

    // Test 1: Create symbol with description
    console.log("Test 1: Symbol with description");
    const sym1 = Symbol("mySymbol");
    console.log(sym1.description);  // mySymbol

    // Test 2: Symbols are unique
    console.log("Test 2: Uniqueness");
    const sym2 = Symbol("mySymbol");
    console.log(sym1 === sym2);  // false

    // Test 3: Symbol.for global registry
    console.log("Test 3: Symbol.for");
    const global1 = Symbol.for("shared");
    const global2 = Symbol.for("shared");
    console.log(global1 === global2);  // true

    // Test 4: Symbol.keyFor
    console.log("Test 4: Symbol.keyFor");
    console.log(Symbol.keyFor(global1));  // shared
    console.log(Symbol.keyFor(sym1));  // undefined (not registered)

    // Test 5: Symbol as object property
    console.log("Test 5: As property key");
    const key = Symbol("key");
    const obj: any = {};
    obj[key] = "value";
    console.log(obj[key]);  // value

    // Test 6: Symbol without description
    console.log("Test 6: No description");
    const noDesc = Symbol();
    console.log(noDesc.description);  // undefined

    console.log("=== Tests Complete ===");
    return 0;
}
