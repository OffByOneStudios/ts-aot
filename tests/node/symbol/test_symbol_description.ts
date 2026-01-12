// Test ES2019 Symbol.prototype.description

function user_main(): number {
    // Symbol with description
    const sym1 = Symbol("mySymbol");
    console.log(sym1.description);  // "mySymbol"

    // Symbol without description
    const sym2 = Symbol();
    const desc2 = sym2.description;
    console.log(desc2 === undefined ? "undefined" : desc2);  // "undefined"

    // Symbol.for with description
    const sym3 = Symbol.for("globalKey");
    console.log(sym3.description);  // "globalKey"

    // Symbol.keyFor returns the key for global symbols
    const key3 = Symbol.keyFor(sym3);
    console.log(key3 === undefined ? "undefined" : key3);  // "globalKey"

    // Symbol.keyFor returns undefined for local symbols
    const key1 = Symbol.keyFor(sym1);
    console.log(key1 === undefined ? "undefined" : key1);  // "undefined"

    return 0;
}
