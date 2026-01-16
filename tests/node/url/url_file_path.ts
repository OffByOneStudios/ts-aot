// Test url.fileURLToPath() and url.pathToFileURL()
import * as url from 'url';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== URL File Path Functions Tests ===");

    // ============================================================================
    // Test 1: fileURLToPath with simple path
    // ============================================================================
    {
        console.log("\nTest 1: fileURLToPath with simple path...");
        const fileUrl = "file:///tmp/test.txt";
        const path = url.fileURLToPath(fileUrl);
        console.log("Input: " + fileUrl);
        console.log("Output: " + path);

        // On Unix, should be /tmp/test.txt
        // On Windows, behavior may vary
        if (path.indexOf("tmp") !== -1 && path.indexOf("test.txt") !== -1) {
            console.log("PASS: fileURLToPath converted URL to path");
            passed++;
        } else {
            console.log("FAIL: fileURLToPath did not convert correctly");
            failed++;
        }
    }

    // ============================================================================
    // Test 2: fileURLToPath with percent-encoded characters
    // ============================================================================
    {
        console.log("\nTest 2: fileURLToPath with percent-encoded characters...");
        const fileUrl = "file:///tmp/my%20file.txt";
        const path = url.fileURLToPath(fileUrl);
        console.log("Input: " + fileUrl);
        console.log("Output: " + path);

        // Should decode %20 to space
        if (path.indexOf("my file.txt") !== -1) {
            console.log("PASS: fileURLToPath decoded percent-encoded space");
            passed++;
        } else {
            console.log("FAIL: fileURLToPath did not decode correctly, got: " + path);
            failed++;
        }
    }

    // ============================================================================
    // Test 3: pathToFileURL with simple path
    // ============================================================================
    {
        console.log("\nTest 3: pathToFileURL with simple path...");
        const path = "/tmp/test.txt";
        const fileUrlObj = url.pathToFileURL(path);
        console.log("Input path: " + path);

        if (fileUrlObj) {
            const href = fileUrlObj.href;
            console.log("Output URL: " + href);

            if (href.indexOf("file://") === 0 && href.indexOf("tmp") !== -1) {
                console.log("PASS: pathToFileURL created file URL");
                passed++;
            } else {
                console.log("FAIL: pathToFileURL did not create correct URL");
                failed++;
            }
        } else {
            console.log("FAIL: pathToFileURL returned null");
            failed++;
        }
    }

    // ============================================================================
    // Test 4: pathToFileURL with URL object input to fileURLToPath
    // ============================================================================
    {
        console.log("\nTest 4: fileURLToPath with URL object...");
        const urlObj = new url.URL("file:///home/user/docs/readme.txt");
        const path = url.fileURLToPath(urlObj);
        console.log("Input URL object href: " + urlObj.href);
        console.log("Output path: " + path);

        if (path.indexOf("readme.txt") !== -1) {
            console.log("PASS: fileURLToPath works with URL object");
            passed++;
        } else {
            console.log("FAIL: fileURLToPath did not work with URL object");
            failed++;
        }
    }

    // ============================================================================
    // Test 5: Round-trip conversion
    // ============================================================================
    {
        console.log("\nTest 5: Round-trip path -> URL -> path...");
        const originalPath = "/var/log/app.log";
        const fileUrlObj = url.pathToFileURL(originalPath);

        if (fileUrlObj) {
            const roundTripPath = url.fileURLToPath(fileUrlObj);
            console.log("Original path: " + originalPath);
            console.log("URL href: " + fileUrlObj.href);
            console.log("Round-trip path: " + roundTripPath);

            // On Windows, path separators change to backslash
            // Check that the path components are preserved
            if (roundTripPath.indexOf("var") !== -1 &&
                roundTripPath.indexOf("log") !== -1 &&
                roundTripPath.indexOf("app.log") !== -1) {
                console.log("PASS: Round-trip conversion preserves path components");
                passed++;
            } else {
                console.log("FAIL: Round-trip conversion changed path");
                failed++;
            }
        } else {
            console.log("FAIL: pathToFileURL returned null");
            failed++;
        }
    }

    // ============================================================================
    // Summary
    // ============================================================================
    console.log("");
    console.log("=== URL File Path Functions Results ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    console.log("Total: " + (passed + failed));

    return failed;
}
