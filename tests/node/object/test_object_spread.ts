// Test ES2018 object spread operator

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Basic object spread
    const obj1 = { a: 1, b: 2 };
    const obj2 = { ...obj1, c: 3 };
    if (obj2.a === 1 && obj2.b === 2 && obj2.c === 3) {
        console.log("PASS: Basic object spread");
        passed++;
    } else {
        console.log("FAIL: Basic object spread");
        failed++;
    }

    // Test 2: Spread with override (later property wins)
    const obj3 = { x: 10, y: 20 };
    const obj4 = { ...obj3, x: 100 };
    if (obj4.x === 100 && obj4.y === 20) {
        console.log("PASS: Spread with override");
        passed++;
    } else {
        console.log("FAIL: Spread with override - got x=" + obj4.x);
        failed++;
    }

    // Test 3: Multiple spreads
    const a = { p: 1 };
    const b = { q: 2 };
    const c = { ...a, ...b, r: 3 };
    if (c.p === 1 && c.q === 2 && c.r === 3) {
        console.log("PASS: Multiple spreads");
        passed++;
    } else {
        console.log("FAIL: Multiple spreads");
        failed++;
    }

    // Test 4: Spread empty object
    const empty = {};
    const withEmpty = { ...empty, val: 42 };
    if (withEmpty.val === 42) {
        console.log("PASS: Spread empty object");
        passed++;
    } else {
        console.log("FAIL: Spread empty object");
        failed++;
    }

    // Test 5: Spread at start position
    const first = { ...{ start: 1 }, middle: 2, end: 3 };
    if (first.start === 1 && first.middle === 2 && first.end === 3) {
        console.log("PASS: Spread at start");
        passed++;
    } else {
        console.log("FAIL: Spread at start");
        failed++;
    }

    // Test 6: Override order matters
    const base = { val: 1 };
    const override1 = { val: 2, ...base };  // base.val wins (spread comes after)
    const override2 = { ...base, val: 2 };  // val: 2 wins (comes after spread)
    if (override1.val === 1 && override2.val === 2) {
        console.log("PASS: Override order");
        passed++;
    } else {
        console.log("FAIL: Override order - got override1.val=" + override1.val + " override2.val=" + override2.val);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
