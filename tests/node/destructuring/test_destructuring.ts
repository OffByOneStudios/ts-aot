// Test ES6 destructuring patterns

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // === Array Destructuring ===

    // Test 1: Basic array destructuring
    const arr1: number[] = [1, 2, 3];
    const [a, b, c] = arr1;
    if (a === 1 && b === 2 && c === 3) {
        console.log("PASS: Basic array destructuring");
        passed++;
    } else {
        console.log("FAIL: Basic array destructuring - got a=" + a + " b=" + b + " c=" + c);
        failed++;
    }

    // Test 2: Array destructuring with skipped elements
    const arr2: number[] = [10, 20, 30, 40];
    const [first, , third] = arr2;
    if (first === 10 && third === 30) {
        console.log("PASS: Array destructuring with skipped elements");
        passed++;
    } else {
        console.log("FAIL: Array destructuring with skipped elements - got first=" + first + " third=" + third);
        failed++;
    }

    // Test 3: Array destructuring with rest element
    const arr3: number[] = [1, 2, 3, 4, 5];
    const [head, ...tail] = arr3;
    if (head === 1 && tail.length === 4) {
        console.log("PASS: Array destructuring with rest element");
        passed++;
    } else {
        console.log("FAIL: Array destructuring with rest element - got head=" + head + " tail.length=" + tail.length);
        failed++;
    }

    // Test 4: Array destructuring with default values
    const arr4: number[] = [100];
    const [x, y = 200] = arr4;
    if (x === 100 && y === 200) {
        console.log("PASS: Array destructuring with default values");
        passed++;
    } else {
        console.log("FAIL: Array destructuring with default values - got x=" + x + " y=" + y);
        failed++;
    }

    // === Object Destructuring ===

    // Test 5: Basic object destructuring
    const obj1 = { p: 10, q: 20, r: 30 };
    const { p, q, r } = obj1;
    if (p === 10 && q === 20 && r === 30) {
        console.log("PASS: Basic object destructuring");
        passed++;
    } else {
        console.log("FAIL: Basic object destructuring - got p=" + p + " q=" + q + " r=" + r);
        failed++;
    }

    // Test 6: Object destructuring with renaming
    const obj2 = { m: 100, n: 200 };
    const { m: renamed1, n: renamed2 } = obj2;
    if (renamed1 === 100 && renamed2 === 200) {
        console.log("PASS: Object destructuring with renaming");
        passed++;
    } else {
        console.log("FAIL: Object destructuring with renaming - got renamed1=" + renamed1 + " renamed2=" + renamed2);
        failed++;
    }

    // Test 7: Object destructuring with default values
    const obj3 = { name: "Alice" };
    const { name, age = 25 } = obj3;
    if (name === "Alice" && age === 25) {
        console.log("PASS: Object destructuring with default values");
        passed++;
    } else {
        console.log("FAIL: Object destructuring with default values - got name=" + name + " age=" + age);
        failed++;
    }

    // Test 8: Empty array destructuring
    const emptyArr: number[] = [];
    const [empty1 = 999] = emptyArr;
    if (empty1 === 999) {
        console.log("PASS: Empty array with default");
        passed++;
    } else {
        console.log("FAIL: Empty array with default - got empty1=" + empty1);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
