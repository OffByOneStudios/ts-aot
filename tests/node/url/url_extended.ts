// Test URL class extended features

function user_main(): number {
    console.log("=== URL Extended Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: URL with base parameter
    console.log("\n1. URL with base parameter:");
    const url1 = new URL("/path", "https://example.com");
    console.log("  new URL('/path', 'https://example.com')");
    console.log("  href = " + url1.href);
    if (url1.href === "https://example.com/path") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'https://example.com/path')");
        failed++;
    }

    // Test 2: url.href
    console.log("\n2. url.href:");
    const url2 = new URL("https://user:pass@example.com:8080/path?query=1#hash");
    console.log("  href = " + url2.href);
    if (url2.href.indexOf("example.com") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 3: url.origin
    console.log("\n3. url.origin:");
    const origin = url2.origin;
    console.log("  origin = " + origin);
    if (origin === "https://example.com:8080") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'https://example.com:8080')");
        failed++;
    }

    // Test 4: url.hash
    console.log("\n4. url.hash:");
    const hash = url2.hash;
    console.log("  hash = " + hash);
    if (hash === "#hash") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected '#hash')");
        failed++;
    }

    // Test 5: url.username and url.password
    console.log("\n5. url.username and url.password:");
    console.log("  username = " + url2.username);
    console.log("  password = " + url2.password);
    if (url2.username === "user" && url2.password === "pass") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 6: url.toString()
    console.log("\n6. url.toString():");
    const str = url2.toString();
    console.log("  toString() = " + str);
    if (str.indexOf("example.com") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 7: url.toJSON()
    console.log("\n7. url.toJSON():");
    const json = url2.toJSON();
    console.log("  toJSON() = " + json);
    if (json.indexOf("example.com") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 8: url.searchParams
    console.log("\n8. url.searchParams:");
    const params = url2.searchParams;
    if (params) {
        console.log("  searchParams exists");
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (searchParams is null)");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All URL extended tests passed!");
    }

    return failed;
}
