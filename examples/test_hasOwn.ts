// Test Object.hasOwn()

function user_main(): number {
    const obj = { a: 1, b: 2 };

    console.log("hasOwn(obj, 'a'): " + Object.hasOwn(obj, "a"));  // true
    console.log("hasOwn(obj, 'b'): " + Object.hasOwn(obj, "b"));  // true
    console.log("hasOwn(obj, 'c'): " + Object.hasOwn(obj, "c"));  // false

    return 0;
}
