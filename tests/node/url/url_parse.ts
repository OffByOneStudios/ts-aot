// Test url.parse() - Legacy URL parsing API
import * as url from 'url';

function user_main(): number {
    console.log("Testing url.parse()...");

    // Test 1: Basic URL parsing
    console.log("Test 1: Basic URL parsing");
    const result1 = url.parse("https://example.com/path?query=value#hash");
    console.log("  protocol: " + result1.protocol);
    console.log("  host: " + result1.host);
    console.log("  hostname: " + result1.hostname);
    console.log("  pathname: " + result1.pathname);
    console.log("  search: " + result1.search);
    console.log("  hash: " + result1.hash);
    console.log("  href: " + result1.href);
    if (result1.protocol !== "https:") {
        console.log("FAIL: Expected protocol 'https:'");
        return 1;
    }
    if (result1.hostname !== "example.com") {
        console.log("FAIL: Expected hostname 'example.com'");
        return 1;
    }
    console.log("  PASS");

    // Test 2: URL with port
    console.log("Test 2: URL with port");
    const result2 = url.parse("http://localhost:3000/api/users");
    console.log("  port: " + result2.port);
    console.log("  host: " + result2.host);
    if (result2.port !== "3000") {
        console.log("FAIL: Expected port '3000'");
        return 1;
    }
    if (result2.host !== "localhost:3000") {
        console.log("FAIL: Expected host 'localhost:3000'");
        return 1;
    }
    console.log("  PASS");

    // Test 3: URL with auth
    console.log("Test 3: URL with auth");
    const result3 = url.parse("ftp://user:pass@ftp.example.com/files");
    console.log("  auth: " + result3.auth);
    console.log("  hostname: " + result3.hostname);
    if (result3.auth !== "user:pass") {
        console.log("FAIL: Expected auth 'user:pass'");
        return 1;
    }
    console.log("  PASS");

    // Test 4: URL without path
    console.log("Test 4: URL without path");
    const result4 = url.parse("https://example.com");
    console.log("  pathname: " + result4.pathname);
    console.log("  path: " + result4.path);
    if (result4.pathname !== "/") {
        console.log("FAIL: Expected pathname '/'");
        return 1;
    }
    console.log("  PASS");

    // Test 5: Protocol-relative URL with slashesDenoteHost=true
    console.log("Test 5: Protocol-relative URL");
    const result5 = url.parse("//example.com/path", false, true);
    console.log("  protocol: '" + result5.protocol + "'");
    console.log("  slashes: " + result5.slashes);
    console.log("  hostname: " + result5.hostname);
    console.log("  pathname: " + result5.pathname);
    if (result5.slashes !== true) {
        console.log("FAIL: Expected slashes to be true");
        return 1;
    }
    if (result5.hostname !== "example.com") {
        console.log("FAIL: Expected hostname 'example.com', got '" + result5.hostname + "'");
        return 1;
    }
    console.log("  PASS");

    // Test 6: Path only (no host)
    console.log("Test 6: Path only (no host)");
    const result6 = url.parse("/path/to/file.txt");
    console.log("  pathname: " + result6.pathname);
    console.log("  hostname: '" + result6.hostname + "'");
    if (result6.pathname !== "/path/to/file.txt") {
        console.log("FAIL: Expected pathname '/path/to/file.txt'");
        return 1;
    }
    console.log("  PASS");

    // Test 7: Query string
    console.log("Test 7: Query string");
    const result7 = url.parse("http://example.com?foo=bar&baz=qux");
    console.log("  search: " + result7.search);
    console.log("  query: " + result7.query);
    if (result7.search !== "?foo=bar&baz=qux") {
        console.log("FAIL: Expected search '?foo=bar&baz=qux'");
        return 1;
    }
    console.log("  PASS");

    // Test 8: Hash only
    console.log("Test 8: Hash only");
    const result8 = url.parse("#section");
    console.log("  hash: " + result8.hash);
    if (result8.hash !== "#section") {
        console.log("FAIL: Expected hash '#section'");
        return 1;
    }
    console.log("  PASS");

    // Test 9: Complex URL
    console.log("Test 9: Complex URL");
    const result9 = url.parse("https://user:password@sub.example.com:8080/path/to/page?key=value&foo=bar#section");
    console.log("  protocol: " + result9.protocol);
    console.log("  auth: " + result9.auth);
    console.log("  host: " + result9.host);
    console.log("  hostname: " + result9.hostname);
    console.log("  port: " + result9.port);
    console.log("  pathname: " + result9.pathname);
    console.log("  search: " + result9.search);
    console.log("  hash: " + result9.hash);
    if (result9.protocol !== "https:") {
        console.log("FAIL: Complex URL protocol mismatch");
        return 1;
    }
    if (result9.hostname !== "sub.example.com") {
        console.log("FAIL: Complex URL hostname mismatch");
        return 1;
    }
    if (result9.port !== "8080") {
        console.log("FAIL: Complex URL port mismatch");
        return 1;
    }
    console.log("  PASS");

    console.log("All url.parse() tests passed!");
    return 0;
}
