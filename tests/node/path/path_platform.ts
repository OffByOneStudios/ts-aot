// Test path.posix and path.win32 namespaces
import * as path from 'path';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== Path Platform Namespace Tests ===");

    // Test 1: path.posix.sep
    {
        const sep = path.posix.sep;
        if (sep === "/") {
            console.log("PASS: path.posix.sep = '/'");
            passed++;
        } else {
            console.log("FAIL: path.posix.sep expected '/', got: " + sep);
            failed++;
        }
    }

    // Test 2: path.win32.sep
    {
        const sep = path.win32.sep;
        if (sep === "\\") {
            console.log("PASS: path.win32.sep = '\\\\'");
            passed++;
        } else {
            console.log("FAIL: path.win32.sep expected '\\\\', got: " + sep);
            failed++;
        }
    }

    // Test 3: path.posix.delimiter
    {
        const delim = path.posix.delimiter;
        if (delim === ":") {
            console.log("PASS: path.posix.delimiter = ':'");
            passed++;
        } else {
            console.log("FAIL: path.posix.delimiter expected ':', got: " + delim);
            failed++;
        }
    }

    // Test 4: path.win32.delimiter
    {
        const delim = path.win32.delimiter;
        if (delim === ";") {
            console.log("PASS: path.win32.delimiter = ';'");
            passed++;
        } else {
            console.log("FAIL: path.win32.delimiter expected ';', got: " + delim);
            failed++;
        }
    }

    // Test 5: path.posix.join
    {
        const result = path.posix.join("foo", "bar", "baz");
        if (result === "foo/bar/baz") {
            console.log("PASS: path.posix.join('foo', 'bar', 'baz') = 'foo/bar/baz'");
            passed++;
        } else {
            console.log("FAIL: path.posix.join expected 'foo/bar/baz', got: " + result);
            failed++;
        }
    }

    // Test 6: path.win32.join
    {
        const result = path.win32.join("foo", "bar", "baz");
        if (result === "foo\\bar\\baz") {
            console.log("PASS: path.win32.join('foo', 'bar', 'baz') = 'foo\\\\bar\\\\baz'");
            passed++;
        } else {
            console.log("FAIL: path.win32.join expected 'foo\\\\bar\\\\baz', got: " + result);
            failed++;
        }
    }

    // Test 7: path.toNamespacedPath (on Windows returns \\?\-prefixed path for absolute paths)
    {
        // On non-Windows or for relative paths, it should return the path unchanged
        const result = path.toNamespacedPath("relative/path");
        if (result === "relative/path" || result === "relative\\path") {
            console.log("PASS: path.toNamespacedPath('relative/path') returns path");
            passed++;
        } else {
            console.log("FAIL: path.toNamespacedPath unexpected result: " + result);
            failed++;
        }
    }

    // Test 8: path.posix.basename
    {
        const result = path.posix.basename("/foo/bar/baz.txt");
        if (result === "baz.txt") {
            console.log("PASS: path.posix.basename('/foo/bar/baz.txt') = 'baz.txt'");
            passed++;
        } else {
            console.log("FAIL: path.posix.basename expected 'baz.txt', got: " + result);
            failed++;
        }
    }

    // Test 9: path.win32.basename
    {
        const result = path.win32.basename("C:\\foo\\bar\\baz.txt");
        if (result === "baz.txt") {
            console.log("PASS: path.win32.basename('C:\\\\foo\\\\bar\\\\baz.txt') = 'baz.txt'");
            passed++;
        } else {
            console.log("FAIL: path.win32.basename expected 'baz.txt', got: " + result);
            failed++;
        }
    }

    // Test 10: path.posix.dirname
    {
        const result = path.posix.dirname("/foo/bar/baz.txt");
        if (result === "/foo/bar") {
            console.log("PASS: path.posix.dirname('/foo/bar/baz.txt') = '/foo/bar'");
            passed++;
        } else {
            console.log("FAIL: path.posix.dirname expected '/foo/bar', got: " + result);
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
