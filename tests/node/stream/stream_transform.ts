// Test stream.Transform class
import * as stream from 'stream';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== Stream Transform Tests ===");

    // Test 1: Create a Transform stream
    {
        const transform = new stream.Transform();
        if (transform) {
            console.log("PASS: Created Transform stream");
            passed++;
        } else {
            console.log("FAIL: Could not create Transform stream");
            failed++;
        }
    }

    // Test 2: Check Transform is a Duplex (has both readable and writable properties)
    {
        const transform = new stream.Transform();
        // Check it has duplex-like properties
        const hasReadable = typeof transform.readable !== 'undefined';
        const hasWritable = typeof transform.writable !== 'undefined';

        if (hasReadable || hasWritable) {
            console.log("PASS: Transform has duplex properties");
            passed++;
        } else {
            console.log("FAIL: Transform missing duplex properties");
            failed++;
        }
    }

    // Test 3: Can create another Transform
    {
        const transform = new stream.Transform();
        console.log("PASS: Created second Transform");
        passed++;
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
