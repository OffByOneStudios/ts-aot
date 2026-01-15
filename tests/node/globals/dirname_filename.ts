// Test __dirname and __filename Node.js globals

function user_main(): number {
    console.log("Testing __dirname and __filename:");

    // __filename should be an absolute path ending with our test file
    console.log("__filename:", __filename);

    // __dirname should be an absolute path to the directory containing our test file
    console.log("__dirname:", __dirname);

    // Verify __filename ends with our test file name
    if (__filename.includes("dirname_filename.ts")) {
        console.log("PASS: __filename contains expected file name");
    } else {
        console.log("FAIL: __filename does not contain expected file name");
        return 1;
    }

    // Verify __dirname ends with "globals" directory
    if (__dirname.includes("globals")) {
        console.log("PASS: __dirname contains expected directory");
    } else {
        console.log("FAIL: __dirname does not contain expected directory");
        return 1;
    }

    // Verify __filename starts with __dirname
    if (__filename.includes(__dirname)) {
        console.log("PASS: __filename starts with __dirname");
    } else {
        console.log("FAIL: __filename should start with __dirname");
        return 1;
    }

    console.log("All tests passed!");
    return 0;
}
