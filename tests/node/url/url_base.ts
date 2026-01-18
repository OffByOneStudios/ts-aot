// Test URL constructor with base parameter
const stats = { passed: 0, failed: 0 };

function test(name: string, fn: () => boolean): void {
    try {
        if (fn()) {
            console.log("PASS: " + name);
            stats.passed = stats.passed + 1;
        } else {
            console.log("FAIL: " + name);
            stats.failed = stats.failed + 1;
        }
    } catch (e) {
        console.log("FAIL: " + name + " (exception)");
        stats.failed = stats.failed + 1;
    }
}

function user_main(): number {
    console.log("Testing URL with base parameter");
    console.log("================================");
    console.log("");

    // Test 1: Absolute path against base
    test("Absolute path /path resolves correctly", () => {
        const url = new URL("/path/to/file", "https://example.com/old/path");
        return url.href === "https://example.com/path/to/file";
    });

    // Test 2: Relative path against base
    test("Relative path resolves correctly", () => {
        const url = new URL("file.txt", "https://example.com/dir/");
        return url.href === "https://example.com/dir/file.txt";
    });

    // Test 3: Relative path with parent directory
    test("Relative path with parent reference", () => {
        const url = new URL("../other/file.txt", "https://example.com/dir/subdir/");
        return url.href === "https://example.com/dir/other/file.txt";
    });

    // Test 4: Query-only URL
    test("Query-only ?query resolves correctly", () => {
        const url = new URL("?newquery=1", "https://example.com/path?oldquery=1");
        return url.href === "https://example.com/path?newquery=1";
    });

    // Test 5: Hash-only URL
    test("Hash-only #hash resolves correctly", () => {
        const url = new URL("#newhash", "https://example.com/path#oldhash");
        return url.href === "https://example.com/path#newhash";
    });

    // Test 6: Protocol-relative URL
    test("Protocol-relative //host resolves correctly", () => {
        const url = new URL("//other.com/path", "https://example.com/");
        return url.href === "https://other.com/path";
    });

    // Test 7: Full URL ignores base
    test("Full URL ignores base", () => {
        const url = new URL("https://different.com/path", "https://example.com/");
        return url.href === "https://different.com/path";
    });

    // Test 8: Relative path from file (not directory)
    test("Relative path from file resolves correctly", () => {
        const url = new URL("other.txt", "https://example.com/dir/file.txt");
        return url.href === "https://example.com/dir/other.txt";
    });

    // Test 9: Empty string resolves to base
    test("Empty string resolves to base", () => {
        const url = new URL("", "https://example.com/path?query#hash");
        return url.href === "https://example.com/path?query#hash";
    });

    // Test 10: Base with port
    test("Base with port preserved", () => {
        const url = new URL("/newpath", "https://example.com:8080/oldpath");
        return url.href === "https://example.com:8080/newpath";
    });

    // Test 11: Base with credentials
    test("Base with credentials preserved", () => {
        const url = new URL("/newpath", "https://user:pass@example.com/oldpath");
        return url.href === "https://user:pass@example.com/newpath";
    });

    // Test 12: Dot segments normalized
    test("Dot segments ./file normalized", () => {
        const url = new URL("./file.txt", "https://example.com/dir/");
        return url.href === "https://example.com/dir/file.txt";
    });

    console.log("");
    console.log("Results: " + stats.passed + "/" + (stats.passed + stats.failed) + " passed");
    return stats.failed > 0 ? 1 : 0;
}
