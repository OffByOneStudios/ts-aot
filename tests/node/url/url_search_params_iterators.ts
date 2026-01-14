// Test URLSearchParams iterator methods

function user_main(): number {
    console.log("=== URLSearchParams Iterator Tests ===");

    // Test 1: keys()
    console.log("\nTest 1: keys()");
    const params1 = new URLSearchParams("foo=1&bar=2&baz=3");
    const keys1 = params1.keys();
    console.log("keys count: " + keys1.length);
    console.log("keys: " + keys1.join(", "));

    // Test 2: values()
    console.log("\nTest 2: values()");
    const params2 = new URLSearchParams("a=10&b=20&c=30");
    const values2 = params2.values();
    console.log("values count: " + values2.length);
    console.log("values: " + values2.join(", "));

    // Test 3: entries() - returns array of [key, value] arrays
    console.log("\nTest 3: entries()");
    const params3 = new URLSearchParams("x=100&y=200");
    const entries3 = params3.entries();
    console.log("entries count: " + entries3.length);
    // Note: entries returns array of arrays, which needs special handling
    // for printing since the inner arrays contain TsStrings

    // Test 4: Empty params
    console.log("\nTest 4: Empty params");
    const emptyParams = new URLSearchParams("");
    console.log("empty keys length: " + emptyParams.keys().length);
    console.log("empty values length: " + emptyParams.values().length);
    console.log("empty entries length: " + emptyParams.entries().length);

    // Test 5: Duplicate keys
    console.log("\nTest 5: Duplicate keys");
    const dupParams = new URLSearchParams("key=val1&key=val2&key=val3");
    console.log("duplicate keys: " + dupParams.keys().join(", "));
    console.log("duplicate values: " + dupParams.values().join(", "));

    // Test 6: URL with searchParams
    console.log("\nTest 6: URL.searchParams iterator methods");
    const url = new URL("https://example.com?name=test&value=123");
    console.log("URL keys: " + url.searchParams.keys().join(", "));
    console.log("URL values: " + url.searchParams.values().join(", "));
    console.log("URL entries count: " + url.searchParams.entries().length);

    // Test 7: Special characters
    console.log("\nTest 7: Special characters");
    const specialParams = new URLSearchParams("greeting=hello+world&name=john");
    console.log("special keys: " + specialParams.keys().join(", "));
    console.log("special values: " + specialParams.values().join(", "));

    console.log("\nPASS: All URLSearchParams iterator tests completed");
    return 0;
}
