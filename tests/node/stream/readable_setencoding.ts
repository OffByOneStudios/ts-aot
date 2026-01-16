// Test readable.setEncoding()
import * as stream from 'stream';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    console.log("=== readable.setEncoding() Tests ===");

    // ============================================================================
    // Test 1: setEncoding returns the stream for chaining
    // ============================================================================
    {
        console.log("\nTest 1: setEncoding returns stream for chaining...");
        const arr: string[] = ["hello", "world"];
        const readable = stream.Readable.from(arr);
        const result = readable.setEncoding('utf8');

        // Should return the stream itself
        if (result === readable) {
            console.log("PASS: setEncoding returns the stream");
            passed++;
        } else {
            console.log("FAIL: setEncoding should return the stream for chaining");
            failed++;
        }
    }

    // ============================================================================
    // Test 2: readableEncoding property reflects the set encoding
    // ============================================================================
    {
        console.log("\nTest 2: readableEncoding reflects set encoding...");
        const arr: string[] = ["test"];
        const readable = stream.Readable.from(arr);

        // Initially encoding should be null/undefined
        const initialEnc = readable.readableEncoding;
        console.log("Initial encoding: " + initialEnc);

        // Set the encoding
        readable.setEncoding('utf8');
        const afterEnc = readable.readableEncoding;
        console.log("After setEncoding('utf8'): " + afterEnc);

        if (afterEnc === 'utf8') {
            console.log("PASS: readableEncoding returns 'utf8' after setEncoding");
            passed++;
        } else {
            console.log("FAIL: readableEncoding should return 'utf8'");
            failed++;
        }
    }

    // ============================================================================
    // Test 3: setEncoding with different encoding
    // ============================================================================
    {
        console.log("\nTest 3: setEncoding with 'hex' encoding...");
        const arr: string[] = ["a", "b", "c"];
        const readable = stream.Readable.from(arr);

        // Set hex encoding
        readable.setEncoding('hex');
        const enc = readable.readableEncoding;
        console.log("After setEncoding('hex'): " + enc);

        if (enc === 'hex') {
            console.log("PASS: setEncoding works with different encodings");
            passed++;
        } else {
            console.log("FAIL: setEncoding should work with 'hex'");
            failed++;
        }
    }

    // ============================================================================
    // Summary
    // ============================================================================
    console.log("");
    console.log("=== readable.setEncoding() Results ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    console.log("Total: " + (passed + failed));

    return failed;
}
