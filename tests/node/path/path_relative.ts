// Test path.relative()
import * as path from 'path';

function user_main(): number {
    console.log("=== Path Relative Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: Same directory
    console.log("\n1. Same directory:");
    const rel1 = path.relative("/home/user", "/home/user");
    console.log("  path.relative('/home/user', '/home/user') = '" + rel1 + "'");
    // Node returns "" but "." is also valid and equivalent
    if (rel1 === "" || rel1 === ".") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected empty string or '.')");
        failed++;
    }

    // Test 2: Child directory
    console.log("\n2. Child directory:");
    const rel2 = path.relative("/home/user", "/home/user/docs");
    console.log("  path.relative('/home/user', '/home/user/docs') = '" + rel2 + "'");
    if (rel2 === "docs") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 'docs')");
        failed++;
    }

    // Test 3: Parent directory
    console.log("\n3. Parent directory:");
    const rel3 = path.relative("/home/user/docs", "/home/user");
    console.log("  path.relative('/home/user/docs', '/home/user') = '" + rel3 + "'");
    if (rel3 === "..") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected '..')");
        failed++;
    }

    // Test 4: Sibling directory
    console.log("\n4. Sibling directory:");
    const rel4 = path.relative("/home/user/docs", "/home/user/images");
    console.log("  path.relative('/home/user/docs', '/home/user/images') = '" + rel4 + "'");
    // Check that it contains ".." and "images"
    if (rel4.indexOf("..") !== -1 && rel4.indexOf("images") !== -1) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected '../images')");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All path.relative tests passed!");
    }

    return failed;
}
