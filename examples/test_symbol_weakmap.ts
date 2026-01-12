// Test symbols as WeakMap keys (ES2023)

function user_main(): number {
    console.log("Symbol WeakMap key test:");

    const sym = Symbol("myKey");
    const wm = new WeakMap();

    // Symbol as key
    const obj = { value: 42 };
    wm.set(sym, obj);

    const retrieved = wm.get(sym);
    if (retrieved) {
        console.log(retrieved.value);  // 42
    }

    console.log(wm.has(sym));  // true

    return 0;
}
