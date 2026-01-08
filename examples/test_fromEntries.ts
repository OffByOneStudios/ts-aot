// Test Object.fromEntries()

function user_main(): number {
    // Test 1: Basic fromEntries
    const entries: [string, number][] = [["a", 1], ["b", 2], ["c", 3]];
    const obj = Object.fromEntries(entries);
    console.log("Test 1 - keys: " + Object.keys(obj).length);

    return 0;
}
