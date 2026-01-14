// Test URLSearchParams class

function user_main(): number {
    console.log("=== URLSearchParams Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Create from string
    console.log("\n1. Create URLSearchParams from string:");
    const params1 = new URLSearchParams("foo=bar&baz=qux");
    console.log("  new URLSearchParams('foo=bar&baz=qux')");
    const foo = params1.get("foo");
    console.log("  get('foo') = " + foo);
    if (foo === "bar") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'bar')");
        failed++;
    }

    // Test 2: has()
    console.log("\n2. has():");
    const hasFoo = params1.has("foo");
    const hasNope = params1.has("nope");
    console.log("  has('foo') = " + hasFoo);
    console.log("  has('nope') = " + hasNope);
    if (hasFoo === true && hasNope === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 3: append()
    console.log("\n3. append():");
    params1.append("new", "value");
    const newVal = params1.get("new");
    console.log("  append('new', 'value')");
    console.log("  get('new') = " + newVal);
    if (newVal === "value") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'value')");
        failed++;
    }

    // Test 4: set()
    console.log("\n4. set():");
    params1.set("foo", "updated");
    const updated = params1.get("foo");
    console.log("  set('foo', 'updated')");
    console.log("  get('foo') = " + updated);
    if (updated === "updated") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'updated')");
        failed++;
    }

    // Test 5: delete()
    console.log("\n5. delete():");
    params1.delete("baz");
    const afterDelete = params1.has("baz");
    console.log("  delete('baz')");
    console.log("  has('baz') = " + afterDelete);
    if (afterDelete === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected false)");
        failed++;
    }

    // Test 6: toString()
    console.log("\n6. toString():");
    const str = params1.toString();
    console.log("  toString() = " + str);
    if (str.indexOf("foo=updated") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 7: size
    console.log("\n7. size:");
    const params2 = new URLSearchParams("a=1&b=2&c=3");
    const size = params2.size;
    console.log("  size = " + size);
    if (size === 3) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 3)");
        failed++;
    }

    // Test 8: getAll()
    console.log("\n8. getAll():");
    const params3 = new URLSearchParams("a=1&a=2&a=3");
    const allA = params3.getAll("a");
    console.log("  getAll('a') length = " + allA.length);
    if (allA.length === 3) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 3)");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All URLSearchParams tests passed!");
    }

    return failed;
}
