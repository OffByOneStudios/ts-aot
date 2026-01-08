// Test Array.flat() functionality

function user_main(): number {
    // Test 1: Basic flat with default depth
    const nested: number[][] = [[1, 2], [3, 4]];
    const flat1 = nested.flat();
    console.log("Test 1 - Basic flat:");
    console.log(flat1.length);  // Should print 4

    // Test 2: Flat with explicit depth 1
    const nested2: number[][] = [[1, 2], [3, 4]];
    const flat2 = nested2.flat(1);
    console.log("Test 2 - Flat depth 1:");
    console.log(flat2.length);  // Should print 4

    // Test 3: Already flat array
    const simple: number[] = [1, 2, 3];
    const flat3 = simple.flat();
    console.log("Test 3 - Already flat:");
    console.log(flat3.length);  // Should print 3

    console.log("Done");
    return 0;
}
