// Test Array.from() functionality

function user_main(): number {
    // Test 1: Basic Array.from with an array source
    const source: number[] = [1, 2, 3];
    const copied = Array.from(source);
    console.log("Test 1 - Copy array:");
    console.log(copied.length);  // Should print 3

    // Test 2: Array.from with a mapFn
    const doubled = Array.from(source, (x: number) => x * 2);
    console.log("Test 2 - With map function:");
    console.log(doubled.length);  // Should print 3

    // Test 3: Array.isArray still works
    const isArr = Array.isArray(copied);
    console.log("Test 3 - isArray:");
    console.log(isArr);  // Should print true

    console.log("Done");
    return 0;
}
