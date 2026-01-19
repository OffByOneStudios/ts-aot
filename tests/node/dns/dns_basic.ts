// Basic DNS module test
import * as dns from 'dns';

let testsRun = 0;
let testsPassed = 0;
let testsCompleted = 0;
const totalTests = 3;

function pass(name: string): void {
    testsPassed++;
    testsCompleted++;
    console.log("PASS: " + name);
    checkDone();
}

function fail(name: string, msg: string): void {
    testsCompleted++;
    console.log("FAIL: " + name + " - " + msg);
    checkDone();
}

function checkDone(): void {
    if (testsCompleted >= totalTests) {
        console.log("");
        console.log("Tests: " + testsPassed + "/" + totalTests + " passed");
    }
}

function user_main(): number {
    console.log("DNS Module Basic Tests");
    console.log("======================");

    // Test 1: dns.lookup with callback
    testsRun++;
    dns.lookup("localhost", (err: any, address: string, family: number) => {
        if (err) {
            fail("dns.lookup localhost", err.message || "unknown error");
        } else if (address === "127.0.0.1" || address === "::1") {
            pass("dns.lookup localhost");
        } else {
            fail("dns.lookup localhost", "unexpected address: " + address);
        }
    });

    // Test 2: dns.getServers
    testsRun++;
    const servers = dns.getServers();
    // getServers returns an array (may be empty if not implemented)
    if (Array.isArray(servers)) {
        pass("dns.getServers returns array");
    } else {
        fail("dns.getServers returns array", "not an array");
    }

    // Test 3: DNS error codes exist
    testsRun++;
    if (typeof dns.NODATA === "number" &&
        typeof dns.NOTFOUND === "number" &&
        typeof dns.CONNREFUSED === "number" &&
        typeof dns.TIMEOUT === "number") {
        pass("DNS error codes defined");
    } else {
        fail("DNS error codes defined", "missing constants");
    }

    return 0;
}
