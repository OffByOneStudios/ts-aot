// Test os.constants functionality
import * as os from 'os';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== os.constants Tests ===");

    // Test 1: os.constants exists
    {
        const constants = os.constants;
        if (constants) {
            console.log("PASS: os.constants exists");
            passed++;
        } else {
            console.log("FAIL: os.constants does not exist");
            failed++;
        }
    }

    // Test 2: os.constants.signals exists
    {
        const signals = os.constants.signals;
        if (signals) {
            console.log("PASS: os.constants.signals exists");
            passed++;
        } else {
            console.log("FAIL: os.constants.signals does not exist");
            failed++;
        }
    }

    // Test 3: SIGTERM signal value
    {
        const sigterm = os.constants.signals.SIGTERM;
        if (sigterm === 15) {
            console.log("PASS: SIGTERM = 15");
            passed++;
        } else {
            console.log("FAIL: SIGTERM expected 15, got: " + sigterm);
            failed++;
        }
    }

    // Test 4: SIGINT signal value
    {
        const sigint = os.constants.signals.SIGINT;
        if (sigint === 2) {
            console.log("PASS: SIGINT = 2");
            passed++;
        } else {
            console.log("FAIL: SIGINT expected 2, got: " + sigint);
            failed++;
        }
    }

    // Test 5: SIGKILL signal value
    {
        const sigkill = os.constants.signals.SIGKILL;
        if (sigkill === 9) {
            console.log("PASS: SIGKILL = 9");
            passed++;
        } else {
            console.log("FAIL: SIGKILL expected 9, got: " + sigkill);
            failed++;
        }
    }

    // Test 6: os.constants.errno exists
    {
        const errno = os.constants.errno;
        if (errno) {
            console.log("PASS: os.constants.errno exists");
            passed++;
        } else {
            console.log("FAIL: os.constants.errno does not exist");
            failed++;
        }
    }

    // Test 7: ENOENT errno value
    {
        const enoent = os.constants.errno.ENOENT;
        if (enoent === 2) {
            console.log("PASS: ENOENT = 2");
            passed++;
        } else {
            console.log("FAIL: ENOENT expected 2, got: " + enoent);
            failed++;
        }
    }

    // Test 8: EACCES errno value
    {
        const eacces = os.constants.errno.EACCES;
        if (eacces === 13) {
            console.log("PASS: EACCES = 13");
            passed++;
        } else {
            console.log("FAIL: EACCES expected 13, got: " + eacces);
            failed++;
        }
    }

    // Test 9: os.constants.priority exists
    {
        const priority = os.constants.priority;
        if (priority) {
            console.log("PASS: os.constants.priority exists");
            passed++;
        } else {
            console.log("FAIL: os.constants.priority does not exist");
            failed++;
        }
    }

    // Test 10: PRIORITY_NORMAL value
    {
        const normal = os.constants.priority.PRIORITY_NORMAL;
        if (normal === 0) {
            console.log("PASS: PRIORITY_NORMAL = 0");
            passed++;
        } else {
            console.log("FAIL: PRIORITY_NORMAL expected 0, got: " + normal);
            failed++;
        }
    }

    // Test 11: PRIORITY_LOW value
    {
        const low = os.constants.priority.PRIORITY_LOW;
        if (low === 19) {
            console.log("PASS: PRIORITY_LOW = 19");
            passed++;
        } else {
            console.log("FAIL: PRIORITY_LOW expected 19, got: " + low);
            failed++;
        }
    }

    // Test 12: PRIORITY_HIGHEST value
    {
        const highest = os.constants.priority.PRIORITY_HIGHEST;
        if (highest === -20) {
            console.log("PASS: PRIORITY_HIGHEST = -20");
            passed++;
        } else {
            console.log("FAIL: PRIORITY_HIGHEST expected -20, got: " + highest);
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
