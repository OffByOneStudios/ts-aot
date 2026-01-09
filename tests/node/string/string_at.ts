// Test String.prototype.at() method

function user_main(): number {
    let passed = 0;
    let failed = 0;

    const str = "Hello";

    // Test 1: Positive index
    const h = str.at(0);
    if (h === "H") {
        console.log("PASS: at(0) returns 'H'");
        passed++;
    } else {
        console.log("FAIL: at(0) expected 'H', got " + h);
        failed++;
    }

    // Test 2: Middle index
    const l = str.at(2);
    if (l === "l") {
        console.log("PASS: at(2) returns 'l'");
        passed++;
    } else {
        console.log("FAIL: at(2) expected 'l', got " + l);
        failed++;
    }

    // Test 3: Last index
    const o = str.at(4);
    if (o === "o") {
        console.log("PASS: at(4) returns 'o'");
        passed++;
    } else {
        console.log("FAIL: at(4) expected 'o', got " + o);
        failed++;
    }

    // Test 4: Negative index (from end)
    const lastChar = str.at(-1);
    if (lastChar === "o") {
        console.log("PASS: at(-1) returns 'o' (last char)");
        passed++;
    } else {
        console.log("FAIL: at(-1) expected 'o', got " + lastChar);
        failed++;
    }

    // Test 5: Negative index -2 (second to last)
    const secondLast = str.at(-2);
    if (secondLast === "l") {
        console.log("PASS: at(-2) returns 'l'");
        passed++;
    } else {
        console.log("FAIL: at(-2) expected 'l', got " + secondLast);
        failed++;
    }

    // Test 6: Negative index -5 (first char)
    const firstFromEnd = str.at(-5);
    if (firstFromEnd === "H") {
        console.log("PASS: at(-5) returns 'H' (first char)");
        passed++;
    } else {
        console.log("FAIL: at(-5) expected 'H', got " + firstFromEnd);
        failed++;
    }

    // Test 7: Compare with charAt for positive index
    const charAtResult = str.charAt(1);
    const atResult = str.at(1);
    if (charAtResult === atResult && charAtResult === "e") {
        console.log("PASS: at(1) equals charAt(1) = 'e'");
        passed++;
    } else {
        console.log("FAIL: at(1) should equal charAt(1)");
        failed++;
    }

    // Test 8: Long string with negative index
    const longStr = "JavaScript";
    const j = longStr.at(-10);
    if (j === "J") {
        console.log("PASS: 'JavaScript'.at(-10) returns 'J'");
        passed++;
    } else {
        console.log("FAIL: 'JavaScript'.at(-10) expected 'J', got " + j);
        failed++;
    }

    console.log("---");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed > 0 ? 1 : 0;
}
