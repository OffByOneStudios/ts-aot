// Test path.parse() and path.format()
import * as path from 'path';

function user_main(): number {
    console.log("=== Path Parse/Format Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: path.parse() on Windows-style path
    console.log("\n1. path.parse() with absolute path:");
    const parsed1 = path.parse("/home/user/file.txt");
    console.log("  Input: /home/user/file.txt");
    console.log("  root: " + parsed1.root);
    console.log("  dir: " + parsed1.dir);
    console.log("  base: " + parsed1.base);
    console.log("  ext: " + parsed1.ext);
    console.log("  name: " + parsed1.name);

    if (parsed1.base === "file.txt" && parsed1.ext === ".txt" && parsed1.name === "file") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 2: path.parse() on file without extension
    console.log("\n2. path.parse() without extension:");
    const parsed2 = path.parse("/etc/passwd");
    console.log("  Input: /etc/passwd");
    console.log("  root: " + parsed2.root);
    console.log("  dir: " + parsed2.dir);
    console.log("  base: " + parsed2.base);
    console.log("  ext: " + parsed2.ext);
    console.log("  name: " + parsed2.name);

    if (parsed2.base === "passwd" && parsed2.ext === "" && parsed2.name === "passwd") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 3: path.parse() on hidden file
    console.log("\n3. path.parse() on hidden file:");
    const parsed3 = path.parse("/home/user/.bashrc");
    console.log("  Input: /home/user/.bashrc");
    console.log("  base: " + parsed3.base);
    console.log("  ext: " + parsed3.ext);
    console.log("  name: " + parsed3.name);

    if (parsed3.base === ".bashrc") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 4: path.format() basic
    console.log("\n4. path.format() basic:");
    const formatted1 = path.format({
        root: "/",
        dir: "/home/user",
        base: "file.txt"
    });
    console.log("  Input: { root: '/', dir: '/home/user', base: 'file.txt' }");
    console.log("  Result: " + formatted1);

    if (formatted1.indexOf("file.txt") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 5: path.format() with name and ext
    console.log("\n5. path.format() with name and ext:");
    const formatted2 = path.format({
        dir: "/tmp",
        name: "output",
        ext: ".log"
    });
    console.log("  Input: { dir: '/tmp', name: 'output', ext: '.log' }");
    console.log("  Result: " + formatted2);

    if (formatted2.indexOf("output") !== -1 && formatted2.indexOf(".log") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 6: Round-trip parse -> format
    console.log("\n6. Round-trip parse -> format:");
    const original = "/var/log/syslog";
    const parsedRT = path.parse(original);
    const formatted = path.format(parsedRT);
    console.log("  Original: " + original);
    console.log("  Parsed then formatted: " + formatted);

    // The formatted path should contain the same filename
    if (formatted.indexOf("syslog") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All path.parse/format tests passed!");
    }

    return failed;
}
