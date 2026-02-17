// Test padEnd/padStart inside template literals

function user_main(): number {
    let failures = 0;

    // Test 1: padEnd inside template literal
    const r1 = `${"hello".padEnd(10)}world`;
    if (r1 === "hello     world") {
        console.log("PASS: padEnd in template literal");
    } else {
        console.log("FAIL: expected 'hello     world', got '" + r1 + "'");
        failures++;
    }

    // Test 2: padStart inside template literal
    const r2 = `${"hi".padStart(10)}!`;
    if (r2 === "        hi!") {
        console.log("PASS: padStart in template literal");
    } else {
        console.log("FAIL: expected '        hi!', got '" + r2 + "'");
        failures++;
    }

    // Test 3: Table row formatting with padEnd
    const header = `${"Name".padEnd(20)}${"Age".padEnd(10)}`;
    if (header === "Name                Age       ") {
        console.log("PASS: table row formatting with padEnd");
    } else {
        console.log("FAIL: header expected 'Name                Age       ', got '" + header + "'");
        failures++;
    }

    // Test 4: padStart with fill character in template
    const r4 = `${"5".padStart(3, "0")}`;
    if (r4 === "005") {
        console.log("PASS: padStart with fill char in template");
    } else {
        console.log("FAIL: expected '005', got '" + r4 + "'");
        failures++;
    }

    // Test 5: Multiple pad operations in one template
    const label = "Price";
    const value = "42";
    const row = `${label.padEnd(15)}${value.padStart(10)}`;
    if (row === "Price                  42") {
        console.log("PASS: multiple pad operations in one template");
    } else {
        console.log("FAIL: expected 'Price                  42', got '" + row + "'");
        failures++;
    }

    console.log("---");
    if (failures === 0) {
        console.log("All string pad template tests passed!");
    } else {
        console.log(failures + " test(s) failed");
    }

    return failures;
}
