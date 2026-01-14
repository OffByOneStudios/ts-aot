// Test os module
import * as os from 'os';

function user_main(): number {
    console.log("=== OS Module Tests ===");
    let passed = 0;
    let failed = 0;

    // Test platform
    console.log("\n1. os.platform():");
    const platform = os.platform();
    console.log("  Result: " + platform);
    if (platform === "win32" || platform === "darwin" || platform === "linux" || platform === "freebsd") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: unexpected platform");
        failed++;
    }

    // Test arch
    console.log("\n2. os.arch():");
    const arch = os.arch();
    console.log("  Result: " + arch);
    if (arch === "x64" || arch === "ia32" || arch === "arm64" || arch === "arm") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: unexpected arch");
        failed++;
    }

    // Test type
    console.log("\n3. os.type():");
    const ostype = os.type();
    console.log("  Result: " + ostype);
    if (ostype === "Windows_NT" || ostype === "Darwin" || ostype === "Linux" || ostype === "FreeBSD") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: unexpected type");
        failed++;
    }

    // Test hostname
    console.log("\n4. os.hostname():");
    const hostname = os.hostname();
    console.log("  Result: " + hostname);
    if (hostname.length > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: empty hostname");
        failed++;
    }

    // Test homedir
    console.log("\n5. os.homedir():");
    const homedir = os.homedir();
    console.log("  Result: " + homedir);
    if (homedir.length > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: empty homedir");
        failed++;
    }

    // Test tmpdir
    console.log("\n6. os.tmpdir():");
    const tmpdir = os.tmpdir();
    console.log("  Result: " + tmpdir);
    if (tmpdir.length > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: empty tmpdir");
        failed++;
    }

    // Test EOL
    console.log("\n7. os.EOL:");
    const eol = os.EOL;
    console.log("  Result: " + JSON.stringify(eol));
    if (eol === "\r\n" || eol === "\n") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: unexpected EOL");
        failed++;
    }

    // Test totalmem
    console.log("\n8. os.totalmem():");
    const totalmem = os.totalmem();
    console.log("  Result: " + totalmem);
    if (totalmem > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: invalid totalmem");
        failed++;
    }

    // Test freemem
    console.log("\n9. os.freemem():");
    const freemem = os.freemem();
    console.log("  Result: " + freemem);
    if (freemem > 0 && freemem <= totalmem) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: invalid freemem");
        failed++;
    }

    // Test uptime
    console.log("\n10. os.uptime():");
    const uptime = os.uptime();
    console.log("  Result: " + uptime);
    if (uptime > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: invalid uptime");
        failed++;
    }

    // Test endianness
    console.log("\n11. os.endianness():");
    const endianness = os.endianness();
    console.log("  Result: " + endianness);
    if (endianness === "LE" || endianness === "BE") {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: unexpected endianness");
        failed++;
    }

    // Test loadavg
    console.log("\n12. os.loadavg():");
    const loadavg = os.loadavg();
    console.log("  Result: [" + loadavg[0] + ", " + loadavg[1] + ", " + loadavg[2] + "]");
    if (loadavg.length === 3) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: expected array of 3");
        failed++;
    }

    // Test cpus
    console.log("\n13. os.cpus():");
    const cpus = os.cpus();
    console.log("  Count: " + cpus.length);
    if (cpus.length > 0) {
        console.log("  First CPU: " + cpus[0].model);
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: no CPUs found");
        failed++;
    }

    // Test userInfo
    console.log("\n14. os.userInfo():");
    const userInfo = os.userInfo();
    console.log("  Username: " + userInfo.username);
    if (userInfo.username && userInfo.username.length > 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL: empty username");
        failed++;
    }

    // Summary
    console.log("\n========================================");
    console.log("Results: " + passed + "/" + (passed + failed) + " tests passed");
    if (failed === 0) {
        console.log("All os module tests passed!");
    }

    return failed;
}
