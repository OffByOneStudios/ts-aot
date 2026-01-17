// Test url.format() - Legacy URL formatting API
import * as url from 'url';

function user_main(): number {
    console.log("Testing url.format()...");

    // Test 1: Format parsed URL back to string
    console.log("Test 1: Format parsed URL");
    const parsed = url.parse("https://example.com/path?query=value#hash");
    const formatted = url.format(parsed);
    console.log("  formatted: " + formatted);
    if (formatted !== "https://example.com/path?query=value#hash") {
        console.log("FAIL: Expected 'https://example.com/path?query=value#hash'");
        return 1;
    }
    console.log("  PASS");

    // Test 2: Format URL with port
    console.log("Test 2: Format URL with port");
    const parsed2 = url.parse("http://localhost:3000/api");
    const formatted2 = url.format(parsed2);
    console.log("  formatted: " + formatted2);
    if (formatted2 !== "http://localhost:3000/api") {
        console.log("FAIL: Expected 'http://localhost:3000/api'");
        return 1;
    }
    console.log("  PASS");

    // Test 3: Format URL with auth
    console.log("Test 3: Format URL with auth");
    const parsed3 = url.parse("ftp://user:pass@ftp.example.com/files");
    const formatted3 = url.format(parsed3);
    console.log("  formatted: " + formatted3);
    if (formatted3 !== "ftp://user:pass@ftp.example.com/files") {
        console.log("FAIL: Expected 'ftp://user:pass@ftp.example.com/files'");
        return 1;
    }
    console.log("  PASS");

    // Test 4: Format URL object (legacy format)
    console.log("Test 4: Format legacy URL object");
    const urlObj: any = {
        protocol: "https:",
        hostname: "example.org",
        port: "8080",
        pathname: "/test"
    };
    const formatted4 = url.format(urlObj);
    console.log("  formatted: " + formatted4);
    // Note: format should produce a valid URL string
    if (!formatted4.includes("example.org")) {
        console.log("FAIL: Expected URL to contain 'example.org'");
        return 1;
    }
    console.log("  PASS");

    console.log("All url.format() tests passed!");
    return 0;
}
