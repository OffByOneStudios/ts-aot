// Extended zlib tests with larger data
import * as zlib from 'zlib';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test with larger data that should compress significantly
    const largeString = "The quick brown fox jumps over the lazy dog. ".repeat(100);
    const largeBuffer = Buffer.from(largeString, 'utf8');
    console.log("Input size: " + largeBuffer.length + " bytes");

    // Test 1: gzip compression ratio
    const gzipped = zlib.gzipSync(largeBuffer);
    console.log("gzip compressed size: " + gzipped.length + " bytes");
    if (gzipped.length < largeBuffer.length / 2) {
        console.log("PASS: gzip achieved significant compression");
        passed++;
    } else {
        console.log("FAIL: gzip compression ratio too low");
        failed++;
    }

    // Verify roundtrip
    const ungzipped = zlib.gunzipSync(gzipped);
    if (ungzipped.toString('utf8') === largeString) {
        console.log("PASS: Large gzip roundtrip successful");
        passed++;
    } else {
        console.log("FAIL: Large gzip roundtrip failed");
        failed++;
    }

    // Test 2: deflate compression ratio
    const deflated = zlib.deflateSync(largeBuffer);
    console.log("deflate compressed size: " + deflated.length + " bytes");
    if (deflated.length < largeBuffer.length / 2) {
        console.log("PASS: deflate achieved significant compression");
        passed++;
    } else {
        console.log("FAIL: deflate compression ratio too low");
        failed++;
    }

    // Verify roundtrip
    const inflated = zlib.inflateSync(deflated);
    if (inflated.toString('utf8') === largeString) {
        console.log("PASS: Large deflate roundtrip successful");
        passed++;
    } else {
        console.log("FAIL: Large deflate roundtrip failed");
        failed++;
    }

    // Test 3: deflateRaw compression ratio
    const deflatedRaw = zlib.deflateRawSync(largeBuffer);
    console.log("deflateRaw compressed size: " + deflatedRaw.length + " bytes");
    if (deflatedRaw.length < largeBuffer.length / 2) {
        console.log("PASS: deflateRaw achieved significant compression");
        passed++;
    } else {
        console.log("FAIL: deflateRaw compression ratio too low");
        failed++;
    }

    // Verify roundtrip
    const inflatedRaw = zlib.inflateRawSync(deflatedRaw);
    if (inflatedRaw.toString('utf8') === largeString) {
        console.log("PASS: Large deflateRaw roundtrip successful");
        passed++;
    } else {
        console.log("FAIL: Large deflateRaw roundtrip failed");
        failed++;
    }

    // Test 4: Brotli compression ratio
    const brotliCompressed = zlib.brotliCompressSync(largeBuffer);
    console.log("brotli compressed size: " + brotliCompressed.length + " bytes");
    if (brotliCompressed.length < largeBuffer.length / 2) {
        console.log("PASS: brotli achieved significant compression");
        passed++;
    } else {
        console.log("FAIL: brotli compression ratio too low");
        failed++;
    }

    // Verify roundtrip
    const brotliDecompressed = zlib.brotliDecompressSync(brotliCompressed);
    if (brotliDecompressed.toString('utf8') === largeString) {
        console.log("PASS: Large brotli roundtrip successful");
        passed++;
    } else {
        console.log("FAIL: Large brotli roundtrip failed");
        failed++;
    }

    // Test 5: Test with binary data (non-text)
    const binaryBuffer = Buffer.alloc(1000);
    for (let i = 0; i < 1000; i++) {
        binaryBuffer.writeUInt8(i % 256, i);
    }
    const binaryGzipped = zlib.gzipSync(binaryBuffer);
    const binaryUngzipped = zlib.gunzipSync(binaryGzipped);

    let binaryMatch = binaryBuffer.length === binaryUngzipped.length;
    if (binaryMatch) {
        for (let i = 0; i < binaryBuffer.length; i++) {
            if (binaryBuffer.readUInt8(i) !== binaryUngzipped.readUInt8(i)) {
                binaryMatch = false;
                break;
            }
        }
    }

    if (binaryMatch) {
        console.log("PASS: Binary data gzip roundtrip successful");
        passed++;
    } else {
        console.log("FAIL: Binary data gzip roundtrip failed");
        failed++;
    }

    console.log("\n=== Results: " + passed + " passed, " + failed + " failed ===");

    return failed > 0 ? 1 : 0;
}
