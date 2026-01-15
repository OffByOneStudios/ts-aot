// Test Buffer variable-length read/write methods and swap methods

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // ============================================================================
    // Test 1: readIntLE - Little-endian signed integer read (variable length)
    // ============================================================================
    {
        const buf = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);

        // Read 1 byte (should be 0x01)
        const v1 = buf.readIntLE(0, 1);
        if (v1 === 1) {
            console.log("PASS: readIntLE 1 byte");
            passed++;
        } else {
            console.log("FAIL: readIntLE 1 byte - expected 1, got " + v1);
            failed++;
        }

        // Read 2 bytes little-endian (0x02, 0x01 at offset 0 = 0x0201 = 513)
        const v2 = buf.readIntLE(0, 2);
        if (v2 === 0x0201) {
            console.log("PASS: readIntLE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: readIntLE 2 bytes - expected 513, got " + v2);
            failed++;
        }

        // Read 3 bytes little-endian
        const v3 = buf.readIntLE(0, 3);
        if (v3 === 0x030201) {
            console.log("PASS: readIntLE 3 bytes");
            passed++;
        } else {
            console.log("FAIL: readIntLE 3 bytes - expected 197121, got " + v3);
            failed++;
        }
    }

    // ============================================================================
    // Test 2: readIntBE - Big-endian signed integer read (variable length)
    // ============================================================================
    {
        const buf = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05, 0x06]);

        // Read 2 bytes big-endian (0x01, 0x02 = 0x0102 = 258)
        const v2 = buf.readIntBE(0, 2);
        if (v2 === 0x0102) {
            console.log("PASS: readIntBE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: readIntBE 2 bytes - expected 258, got " + v2);
            failed++;
        }

        // Read 3 bytes big-endian
        const v3 = buf.readIntBE(0, 3);
        if (v3 === 0x010203) {
            console.log("PASS: readIntBE 3 bytes");
            passed++;
        } else {
            console.log("FAIL: readIntBE 3 bytes - expected 66051, got " + v3);
            failed++;
        }
    }

    // ============================================================================
    // Test 3: readUIntLE - Little-endian unsigned integer read
    // ============================================================================
    {
        const buf = Buffer.from([0xFF, 0x02, 0x03]);

        // Read 1 byte (should be 255, not -1)
        const v1 = buf.readUIntLE(0, 1);
        if (v1 === 255) {
            console.log("PASS: readUIntLE 1 byte");
            passed++;
        } else {
            console.log("FAIL: readUIntLE 1 byte - expected 255, got " + v1);
            failed++;
        }

        // Read 2 bytes little-endian
        const v2 = buf.readUIntLE(0, 2);
        if (v2 === 0x02FF) {
            console.log("PASS: readUIntLE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: readUIntLE 2 bytes - expected 767, got " + v2);
            failed++;
        }
    }

    // ============================================================================
    // Test 4: readUIntBE - Big-endian unsigned integer read
    // ============================================================================
    {
        const buf = Buffer.from([0xFF, 0x02, 0x03]);

        // Read 2 bytes big-endian
        const v2 = buf.readUIntBE(0, 2);
        if (v2 === 0xFF02) {
            console.log("PASS: readUIntBE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: readUIntBE 2 bytes - expected 65282, got " + v2);
            failed++;
        }
    }

    // ============================================================================
    // Test 5: writeIntLE - Little-endian signed integer write
    // ============================================================================
    {
        const buf = Buffer.alloc(6);

        // Write 2 bytes little-endian
        buf.writeIntLE(0x0102, 0, 2);
        if (buf[0] === 0x02 && buf[1] === 0x01) {
            console.log("PASS: writeIntLE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: writeIntLE 2 bytes - got [" + buf[0] + ", " + buf[1] + "]");
            failed++;
        }

        // Write 3 bytes little-endian
        buf.writeIntLE(0x030201, 2, 3);
        if (buf[2] === 0x01 && buf[3] === 0x02 && buf[4] === 0x03) {
            console.log("PASS: writeIntLE 3 bytes");
            passed++;
        } else {
            console.log("FAIL: writeIntLE 3 bytes - got [" + buf[2] + ", " + buf[3] + ", " + buf[4] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 6: writeIntBE - Big-endian signed integer write
    // ============================================================================
    {
        const buf = Buffer.alloc(4);

        // Write 2 bytes big-endian
        buf.writeIntBE(0x0102, 0, 2);
        if (buf[0] === 0x01 && buf[1] === 0x02) {
            console.log("PASS: writeIntBE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: writeIntBE 2 bytes - got [" + buf[0] + ", " + buf[1] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 7: writeUIntLE - Little-endian unsigned integer write
    // ============================================================================
    {
        const buf = Buffer.alloc(3);

        // Write 2 bytes little-endian (unsigned)
        buf.writeUIntLE(0xFF02, 0, 2);
        if (buf[0] === 0x02 && buf[1] === 0xFF) {
            console.log("PASS: writeUIntLE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: writeUIntLE 2 bytes - got [" + buf[0] + ", " + buf[1] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 8: writeUIntBE - Big-endian unsigned integer write
    // ============================================================================
    {
        const buf = Buffer.alloc(3);

        // Write 2 bytes big-endian (unsigned)
        buf.writeUIntBE(0xFF02, 0, 2);
        if (buf[0] === 0xFF && buf[1] === 0x02) {
            console.log("PASS: writeUIntBE 2 bytes");
            passed++;
        } else {
            console.log("FAIL: writeUIntBE 2 bytes - got [" + buf[0] + ", " + buf[1] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 9: swap16 - Swap bytes in 16-bit words
    // ============================================================================
    {
        const buf = Buffer.from([0x01, 0x02, 0x03, 0x04]);
        buf.swap16();
        if (buf[0] === 0x02 && buf[1] === 0x01 && buf[2] === 0x04 && buf[3] === 0x03) {
            console.log("PASS: swap16");
            passed++;
        } else {
            console.log("FAIL: swap16 - got [" + buf[0] + ", " + buf[1] + ", " + buf[2] + ", " + buf[3] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 10: swap32 - Swap bytes in 32-bit words
    // ============================================================================
    {
        const buf = Buffer.from([0x01, 0x02, 0x03, 0x04]);
        buf.swap32();
        if (buf[0] === 0x04 && buf[1] === 0x03 && buf[2] === 0x02 && buf[3] === 0x01) {
            console.log("PASS: swap32");
            passed++;
        } else {
            console.log("FAIL: swap32 - got [" + buf[0] + ", " + buf[1] + ", " + buf[2] + ", " + buf[3] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 11: swap64 - Swap bytes in 64-bit words
    // ============================================================================
    {
        const buf = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]);
        buf.swap64();
        if (buf[0] === 0x08 && buf[1] === 0x07 && buf[2] === 0x06 && buf[3] === 0x05 &&
            buf[4] === 0x04 && buf[5] === 0x03 && buf[6] === 0x02 && buf[7] === 0x01) {
            console.log("PASS: swap64");
            passed++;
        } else {
            console.log("FAIL: swap64 - got [" + buf[0] + ", " + buf[1] + ", " + buf[2] + ", " + buf[3] + ", " +
                buf[4] + ", " + buf[5] + ", " + buf[6] + ", " + buf[7] + "]");
            failed++;
        }
    }

    // ============================================================================
    // Test 12: Negative value with readIntLE (sign extension)
    // ============================================================================
    {
        const buf = Buffer.from([0xFF, 0xFF]); // -1 in 2-byte signed
        const v = buf.readIntLE(0, 2);
        if (v === -1) {
            console.log("PASS: readIntLE negative value");
            passed++;
        } else {
            console.log("FAIL: readIntLE negative value - expected -1, got " + v);
            failed++;
        }
    }

    // ============================================================================
    // Summary
    // ============================================================================
    console.log("");
    console.log("=== Buffer Variable-Length Tests ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);
    console.log("Total: " + (passed + failed));

    return failed;
}
