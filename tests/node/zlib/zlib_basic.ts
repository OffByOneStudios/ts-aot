// Basic zlib compression/decompression tests
import * as zlib from 'zlib';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: zlib.constants exists
    const constants = zlib.constants;
    if (constants.Z_NO_FLUSH === 0 && constants.Z_BEST_COMPRESSION === 9) {
        console.log("PASS: zlib.constants has expected values");
        passed++;
    } else {
        console.log("FAIL: zlib.constants values incorrect");
        failed++;
    }

    // Test 2: gzip/gunzip roundtrip
    const testString = "Hello, zlib compression!";
    const inputBuffer = Buffer.from(testString, 'utf8');

    const compressed = zlib.gzipSync(inputBuffer);
    if (compressed && compressed.length > 0) {
        console.log("PASS: gzipSync produced output, length=" + compressed.length);
        passed++;

        // Decompress
        const decompressed = zlib.gunzipSync(compressed);
        const result = decompressed.toString('utf8');
        if (result === testString) {
            console.log("PASS: gzip/gunzip roundtrip successful");
            passed++;
        } else {
            console.log("FAIL: gzip/gunzip roundtrip failed, got: " + result);
            failed++;
        }
    } else {
        console.log("FAIL: gzipSync failed");
        failed++;
    }

    // Test 3: deflate/inflate roundtrip
    const deflated = zlib.deflateSync(inputBuffer);
    if (deflated && deflated.length > 0) {
        console.log("PASS: deflateSync produced output, length=" + deflated.length);
        passed++;

        const inflated = zlib.inflateSync(deflated);
        const result2 = inflated.toString('utf8');
        if (result2 === testString) {
            console.log("PASS: deflate/inflate roundtrip successful");
            passed++;
        } else {
            console.log("FAIL: deflate/inflate roundtrip failed, got: " + result2);
            failed++;
        }
    } else {
        console.log("FAIL: deflateSync failed");
        failed++;
    }

    // Test 4: deflateRaw/inflateRaw roundtrip
    const deflatedRaw = zlib.deflateRawSync(inputBuffer);
    if (deflatedRaw && deflatedRaw.length > 0) {
        console.log("PASS: deflateRawSync produced output, length=" + deflatedRaw.length);
        passed++;

        const inflatedRaw = zlib.inflateRawSync(deflatedRaw);
        const result3 = inflatedRaw.toString('utf8');
        if (result3 === testString) {
            console.log("PASS: deflateRaw/inflateRaw roundtrip successful");
            passed++;
        } else {
            console.log("FAIL: deflateRaw/inflateRaw roundtrip failed, got: " + result3);
            failed++;
        }
    } else {
        console.log("FAIL: deflateRawSync failed");
        failed++;
    }

    // Test 5: Brotli compression/decompression
    const brotliCompressed = zlib.brotliCompressSync(inputBuffer);
    if (brotliCompressed && brotliCompressed.length > 0) {
        console.log("PASS: brotliCompressSync produced output, length=" + brotliCompressed.length);
        passed++;

        const brotliDecompressed = zlib.brotliDecompressSync(brotliCompressed);
        const result4 = brotliDecompressed.toString('utf8');
        if (result4 === testString) {
            console.log("PASS: brotli roundtrip successful");
            passed++;
        } else {
            console.log("FAIL: brotli roundtrip failed, got: " + result4);
            failed++;
        }
    } else {
        console.log("FAIL: brotliCompressSync failed");
        failed++;
    }

    // Test 6: unzip (handles both gzip and deflate)
    const gzipped = zlib.gzipSync(inputBuffer);
    const unzipped = zlib.unzipSync(gzipped);
    const unzipResult = unzipped.toString('utf8');
    if (unzipResult === testString) {
        console.log("PASS: unzipSync with gzip input successful");
        passed++;
    } else {
        console.log("FAIL: unzipSync failed, got: " + unzipResult);
        failed++;
    }

    // Test 7: Compression with options
    const highCompression = zlib.gzipSync(inputBuffer, { level: 9 });
    const lowCompression = zlib.gzipSync(inputBuffer, { level: 1 });
    console.log("INFO: High compression length=" + highCompression.length + ", low=" + lowCompression.length);
    passed++; // Just verify it doesn't crash

    console.log("\n=== Results: " + passed + " passed, " + failed + " failed ===");

    return failed > 0 ? 1 : 0;
}
