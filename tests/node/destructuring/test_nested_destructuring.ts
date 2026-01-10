// Test nested destructuring patterns (ES6)

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // === Nested Object Destructuring ===

    // Test 1: Nested object destructuring
    const data1 = { user: { name: "Alice", age: 30 } };
    const { user: { name, age } } = data1;
    if (name === "Alice" && age === 30) {
        console.log("PASS: Nested object destructuring");
        passed++;
    } else {
        console.log("FAIL: Nested object destructuring - got name=" + name + " age=" + age);
        failed++;
    }

    // Test 2: Deep nested object destructuring
    const data2 = { a: { b: { c: { d: 42 } } } };
    const { a: { b: { c: { d } } } } = data2;
    if (d === 42) {
        console.log("PASS: Deep nested object destructuring");
        passed++;
    } else {
        console.log("FAIL: Deep nested object destructuring - got d=" + d);
        failed++;
    }

    // Test 3: Nested object with renaming
    const data3 = { outer: { inner: 100 } };
    const { outer: { inner: renamed } } = data3;
    if (renamed === 100) {
        console.log("PASS: Nested object with renaming");
        passed++;
    } else {
        console.log("FAIL: Nested object with renaming - got renamed=" + renamed);
        failed++;
    }

    // === Nested Array Destructuring ===

    // Test 4: Nested array destructuring
    const arr1: number[][] = [[1, 2], [3, 4]];
    const [[a, b], [c, d2]] = arr1;
    if (a === 1 && b === 2 && c === 3 && d2 === 4) {
        console.log("PASS: Nested array destructuring");
        passed++;
    } else {
        console.log("FAIL: Nested array destructuring - got a=" + a + " b=" + b + " c=" + c + " d2=" + d2);
        failed++;
    }

    // Test 5: Array with skipped nested element
    const arr2: number[][] = [[10, 20], [30, 40], [50, 60]];
    const [[first], , [fifth]] = arr2;
    if (first === 10 && fifth === 50) {
        console.log("PASS: Array with skipped nested element");
        passed++;
    } else {
        console.log("FAIL: Array with skipped nested element - got first=" + first + " fifth=" + fifth);
        failed++;
    }

    // === Mixed Nesting (Object containing Array) ===

    // Test 6: Object containing array
    const data4 = { items: [100, 200, 300] };
    const { items: [i1, i2, i3] } = data4;
    if (i1 === 100 && i2 === 200 && i3 === 300) {
        console.log("PASS: Object containing array");
        passed++;
    } else {
        console.log("FAIL: Object containing array - got i1=" + i1 + " i2=" + i2 + " i3=" + i3);
        failed++;
    }

    // Test 7: Array containing objects
    const arr3 = [{ x: 1 }, { x: 2 }];
    const [{ x: x1 }, { x: x2 }] = arr3;
    if (x1 === 1 && x2 === 2) {
        console.log("PASS: Array containing objects");
        passed++;
    } else {
        console.log("FAIL: Array containing objects - got x1=" + x1 + " x2=" + x2);
        failed++;
    }

    // Test 8: Complex mixed nesting
    const complex = {
        level1: {
            arr: [{ val: 10 }, { val: 20 }],
            num: 5
        }
    };
    const { level1: { arr: [{ val: v1 }, { val: v2 }], num } } = complex;
    if (v1 === 10 && v2 === 20 && num === 5) {
        console.log("PASS: Complex mixed nesting");
        passed++;
    } else {
        console.log("FAIL: Complex mixed nesting - got v1=" + v1 + " v2=" + v2 + " num=" + num);
        failed++;
    }

    // === Nested with Default Values ===

    // Test 9: Nested object with default
    const data5 = { outer: {} };
    const { outer: { missing = 999 } } = data5;
    if (missing === 999) {
        console.log("PASS: Nested object with default");
        passed++;
    } else {
        console.log("FAIL: Nested object with default - got missing=" + missing);
        failed++;
    }

    // Test 10: Nested array with default
    const arr4: number[][] = [[1]];
    const [[e1, e2 = 50]] = arr4;
    if (e1 === 1 && e2 === 50) {
        console.log("PASS: Nested array with default");
        passed++;
    } else {
        console.log("FAIL: Nested array with default - got e1=" + e1 + " e2=" + e2);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
