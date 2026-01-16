import * as url from 'url';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== URL Module Functions Tests ===");

    // Test 1: url.format() with WHATWG URL
    {
        console.log("Test 1: url.format() with WHATWG URL");
        const myUrl = new url.URL("https://example.com:8080/path?query=value#hash");
        const formatted = url.format(myUrl);
        if (formatted === "https://example.com:8080/path?query=value#hash") {
            console.log("PASS: format(URL) returns correct href");
            passed++;
        } else {
            console.log("FAIL: format(URL) returned: " + formatted);
            failed++;
        }
    }

    // Test 2: url.format() with simple legacy object
    {
        console.log("Test 2: url.format() with legacy object");
        const legacyUrl = {
            protocol: "https:",
            hostname: "example.com",
            port: "443",
            pathname: "/path"
        };
        const formatted = url.format(legacyUrl);
        // Should produce https://example.com:443/path
        if (formatted.indexOf("https://") === 0 && formatted.indexOf("example.com") > 0) {
            console.log("PASS: format(legacyObj) produces URL string");
            passed++;
        } else {
            console.log("FAIL: format(legacyObj) returned: " + formatted);
            failed++;
        }
    }

    // Test 3: url.resolve() - basic relative resolution
    {
        console.log("Test 3: url.resolve() basic");
        const resolved = url.resolve("https://example.com/one/two", "/three");
        if (resolved === "https://example.com/three") {
            console.log("PASS: resolve(base, absolute) works");
            passed++;
        } else {
            console.log("FAIL: resolve returned: " + resolved);
            failed++;
        }
    }

    // Test 4: url.resolve() - relative path
    {
        console.log("Test 4: url.resolve() relative path");
        const resolved = url.resolve("https://example.com/one/two", "three");
        if (resolved === "https://example.com/one/three") {
            console.log("PASS: resolve(base, relative) works");
            passed++;
        } else {
            console.log("FAIL: resolve returned: " + resolved);
            failed++;
        }
    }

    // Test 5: url.resolve() - protocol-relative URL
    {
        console.log("Test 5: url.resolve() protocol-relative");
        const resolved = url.resolve("https://example.com/one", "//other.com/two");
        if (resolved === "https://other.com/two") {
            console.log("PASS: resolve(base, //url) preserves protocol");
            passed++;
        } else {
            console.log("FAIL: resolve returned: " + resolved);
            failed++;
        }
    }

    // Test 6: url.urlToHttpOptions()
    {
        console.log("Test 6: url.urlToHttpOptions()");
        const myUrl = new url.URL("https://user:pass@example.com:8080/path?query=value#hash");
        const options = url.urlToHttpOptions(myUrl);

        // Check that options object has expected properties
        let hasProtocol = false;
        let hasHostname = false;

        // Simple check - options should be an object with some properties
        if (options) {
            console.log("PASS: urlToHttpOptions returns an object");
            passed++;
        } else {
            console.log("FAIL: urlToHttpOptions returned null/undefined");
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");
    return failed;
}
