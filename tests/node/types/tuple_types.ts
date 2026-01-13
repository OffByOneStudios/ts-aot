// Test tuple types (TypeScript)

function user_main(): number {
    console.log("=== Tuple Type Test ===");

    // Test 1: Basic tuple
    console.log("Test 1: Basic tuple");
    const point: [number, number] = [10, 20];
    console.log(point[0]);  // 10
    console.log(point[1]);  // 20

    // Test 2: Mixed type tuple
    console.log("Test 2: Mixed types");
    const person: [string, number] = ["Alice", 30];
    console.log(person[0]);  // Alice
    console.log(person[1]);  // 30

    // Test 3: Tuple with more elements
    console.log("Test 3: Three elements");
    const rgb: [number, number, number] = [255, 128, 0];
    console.log(rgb[0]);  // 255
    console.log(rgb[1]);  // 128
    console.log(rgb[2]);  // 0

    // Test 4: Tuple destructuring
    console.log("Test 4: Destructuring");
    const [x, y] = point;
    console.log(x);  // 10
    console.log(y);  // 20

    // Test 5: Tuple length
    console.log("Test 5: Length");
    console.log(point.length);  // 2

    // Test 6: Tuple with rest element
    console.log("Test 6: Rest element");
    const withRest: [string, ...number[]] = ["hello", 1, 2, 3, 4];
    console.log(withRest[0]);  // hello
    console.log(withRest.length);  // 5

    // Test 7: Optional tuple elements
    console.log("Test 7: Optional elements");
    const optional: [number, string?] = [42];
    console.log(optional[0]);  // 42
    console.log(optional.length);  // 1

    console.log("=== Tests Complete ===");
    return 0;
}
