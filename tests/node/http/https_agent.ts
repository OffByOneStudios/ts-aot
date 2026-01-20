// Test for HTTPS Agent and globalAgent
import * as https from 'https';

function user_main(): number {
    let testsPassed = 0;

    // Test 1: https.Agent constructor
    console.log("Testing https.Agent...");
    const agent = new https.Agent({
        keepAlive: true,
        maxSockets: 10
    });
    if (agent !== null && agent !== undefined) {
        console.log("PASS: https.Agent constructor works");
        testsPassed++;
    } else {
        console.log("FAIL: https.Agent constructor returned null/undefined");
    }

    // Test 2: https.globalAgent
    console.log("Testing https.globalAgent...");
    const globalAgent = https.globalAgent;
    if (globalAgent !== null && globalAgent !== undefined) {
        console.log("PASS: https.globalAgent is available");
        testsPassed++;
    } else {
        console.log("FAIL: https.globalAgent is null/undefined");
    }

    console.log(`Tests passed: ${testsPassed}/2`);
    return testsPassed === 2 ? 0 : 1;
}
