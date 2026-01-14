// Test net module utilities
import * as net from 'net';

function user_main(): number {
    console.log("=== Net Utilities Tests ===");
    let passed = 0;
    let failed = 0;

    // Test 1: net.isIP with IPv4
    console.log("\n1. net.isIP with IPv4:");
    const ip4Result = net.isIP("192.168.1.1");
    console.log("  net.isIP('192.168.1.1') = " + ip4Result);
    if (ip4Result === 4) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 4)");
        failed++;
    }

    // Test 2: net.isIP with IPv6
    console.log("\n2. net.isIP with IPv6:");
    const ip6Result = net.isIP("::1");
    console.log("  net.isIP('::1') = " + ip6Result);
    if (ip6Result === 6) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 6)");
        failed++;
    }

    // Test 3: net.isIP with invalid
    console.log("\n3. net.isIP with invalid:");
    const invalidResult = net.isIP("not.an.ip");
    console.log("  net.isIP('not.an.ip') = " + invalidResult);
    if (invalidResult === 0) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL (expected 0)");
        failed++;
    }

    // Test 4: net.isIPv4
    console.log("\n4. net.isIPv4:");
    const isV4 = net.isIPv4("192.168.1.1");
    const isNotV4 = net.isIPv4("::1");
    console.log("  net.isIPv4('192.168.1.1') = " + isV4);
    console.log("  net.isIPv4('::1') = " + isNotV4);
    if (isV4 === true && isNotV4 === false) {
        console.log("  PASS");
        passed++;
    } else {
        console.log("  FAIL");
        failed++;
    }

    // Test 5: net.isIPv6
    console.log("\n5. net.isIPv6:");
    const isV6 = net.isIPv6("::1");
    const isNotV6 = net.isIPv6("192.168.1.1");
    console.log("  net.isIPv6('::1') = " + isV6);
    console.log("  net.isIPv6('192.168.1.1') = " + isNotV6);
    if (isV6 === true && isNotV6 === false) {
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
        console.log("All net utilities tests passed!");
    }

    return failed;
}
