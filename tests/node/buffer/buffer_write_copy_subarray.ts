// Buffer write, copy, and subarray default-argument regression tests
// Tests root causes: buf.write() with encoding as 3rd arg, buf.copy() with
// omitted sourceEnd, buf.subarray() with omitted end, buf.slice() with
// omitted end, and buf.fill() with omitted end.

function user_main(): number {
    let failures = 0;
    console.log('=== Buffer write/copy/subarray defaults ===\n');

    // ---------- buf.write() ----------

    // Test 1: buf.write(string, offset, encoding) — 3 args, encoding not length
    try {
        const buf = Buffer.alloc(16);
        buf.write('HELLO', 0, 'utf8');
        if (buf[0] === 72 && buf[1] === 69 && buf[4] === 79) {
            console.log('PASS: buf.write with 3 args (string, offset, encoding)');
        } else {
            console.log('FAIL: buf.write with 3 args: got ' + buf[0] + ',' + buf[1] + ',' + buf[4]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.write 3 args - Exception');
        failures++;
    }

    // Test 2: buf.write(string, offset) — writes full string
    try {
        const buf = Buffer.alloc(16);
        buf.write('ABCD', 4);
        if (buf[4] === 65 && buf[5] === 66 && buf[6] === 67 && buf[7] === 68) {
            console.log('PASS: buf.write with 2 args (string, offset)');
        } else {
            console.log('FAIL: buf.write with 2 args: got ' + buf[4] + ',' + buf[5]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.write 2 args - Exception');
        failures++;
    }

    // Test 3: buf.write(string) — writes at offset 0
    try {
        const buf = Buffer.alloc(8);
        buf.write('XY');
        if (buf[0] === 88 && buf[1] === 89 && buf[2] === 0) {
            console.log('PASS: buf.write with 1 arg (string)');
        } else {
            console.log('FAIL: buf.write with 1 arg: got ' + buf[0] + ',' + buf[1]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.write 1 arg - Exception');
        failures++;
    }

    // ---------- buf.copy() ----------

    // Test 4: src.copy(target, targetStart) — omitted sourceStart/sourceEnd
    try {
        const src = Buffer.from('ABCDEFGH');
        const dst = Buffer.alloc(16);
        src.copy(dst, 4);
        if (dst[4] === 65 && dst[11] === 72 && dst[3] === 0) {
            console.log('PASS: buf.copy with 2 args (target, targetStart)');
        } else {
            console.log('FAIL: buf.copy with 2 args: got dst[4]=' + dst[4] + ', dst[11]=' + dst[11]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.copy 2 args - Exception');
        failures++;
    }

    // Test 5: src.copy(target) — copies full source to start of target
    try {
        const src = Buffer.from('XYZ');
        const dst = Buffer.alloc(8);
        src.copy(dst);
        if (dst[0] === 88 && dst[1] === 89 && dst[2] === 90 && dst[3] === 0) {
            console.log('PASS: buf.copy with 1 arg (target)');
        } else {
            console.log('FAIL: buf.copy with 1 arg: got ' + dst[0] + ',' + dst[1] + ',' + dst[2]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.copy 1 arg - Exception');
        failures++;
    }

    // Test 6: src.copy(target, tStart, sStart, sEnd) — all 4 args explicit
    try {
        const src = Buffer.from('ABCDEFGH');
        const dst = Buffer.alloc(8);
        src.copy(dst, 0, 2, 5);  // copy 'CDE'
        if (dst[0] === 67 && dst[1] === 68 && dst[2] === 69 && dst[3] === 0) {
            console.log('PASS: buf.copy with 4 args (explicit range)');
        } else {
            console.log('FAIL: buf.copy with 4 args: got ' + dst[0] + ',' + dst[1] + ',' + dst[2]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.copy 4 args - Exception');
        failures++;
    }

    // ---------- buf.subarray() ----------

    // Test 7: buf.subarray(start) — omitted end, should go to buffer length
    try {
        const buf = Buffer.from('ABCDEFGH');
        const sub = buf.subarray(3);
        if (sub.length === 5 && sub[0] === 68 && sub[4] === 72) {
            console.log('PASS: buf.subarray with 1 arg (start only)');
        } else {
            console.log('FAIL: buf.subarray 1 arg: length=' + sub.length + ', sub[0]=' + sub[0]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.subarray 1 arg - Exception');
        failures++;
    }

    // Test 8: buf.subarray(start, end) — both args explicit
    try {
        const buf = Buffer.from('ABCDEFGH');
        const sub = buf.subarray(2, 5);
        if (sub.length === 3 && sub[0] === 67 && sub[2] === 69) {
            console.log('PASS: buf.subarray with 2 args (start, end)');
        } else {
            console.log('FAIL: buf.subarray 2 args: length=' + sub.length + ', sub[0]=' + sub[0]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.subarray 2 args - Exception');
        failures++;
    }

    // ---------- buf.slice() ----------

    // Test 9: buf.slice(start) — omitted end
    try {
        const buf = Buffer.from('ABCDEFGH');
        const sliced = buf.slice(5);
        if (sliced.length === 3 && sliced[0] === 70 && sliced[2] === 72) {
            console.log('PASS: buf.slice with 1 arg (start only)');
        } else {
            console.log('FAIL: buf.slice 1 arg: length=' + sliced.length + ', sliced[0]=' + sliced[0]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.slice 1 arg - Exception');
        failures++;
    }

    // ---------- buf.fill() ----------

    // Test 10: buf.fill(value) — fills entire buffer
    try {
        const buf = Buffer.alloc(8);
        buf.fill(42);
        if (buf[0] === 42 && buf[7] === 42) {
            console.log('PASS: buf.fill with 1 arg (fills entire buffer)');
        } else {
            console.log('FAIL: buf.fill 1 arg: got ' + buf[0] + ',' + buf[7]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.fill 1 arg - Exception');
        failures++;
    }

    // Test 11: buf.fill(value, start) — fills from start to end
    try {
        const buf = Buffer.alloc(8);
        buf.fill(99, 3);
        if (buf[2] === 0 && buf[3] === 99 && buf[7] === 99) {
            console.log('PASS: buf.fill with 2 args (start only)');
        } else {
            console.log('FAIL: buf.fill 2 args: got ' + buf[2] + ',' + buf[3] + ',' + buf[7]);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: buf.fill 2 args - Exception');
        failures++;
    }

    // ---------- Combined: serialize/deserialize header pattern ----------

    // Test 12: Write fields into buffer, read them back (vault header pattern)
    try {
        const header = Buffer.alloc(32);
        header.write('MAGIC123', 0, 'utf8');
        const payload = Buffer.from('PAYLOAD!');
        payload.copy(header, 8);
        header.writeUInt32BE(12345, 16);
        header.writeUInt16BE(7, 20);

        // Read back
        const magic = header.subarray(0, 8).toString();
        const copied = header.subarray(8, 16).toString();
        const num = header.readUInt32BE(16);
        const small = header.readUInt16BE(20);

        if (magic === 'MAGIC123' && copied === 'PAYLOAD!' && small === 7) {
            console.log('PASS: Header serialize/deserialize round-trip');
        } else {
            console.log('FAIL: Header round-trip: magic=' + magic + ', copied=' + copied + ', small=' + small);
            failures++;
        }
    } catch (e) {
        console.log('FAIL: Header round-trip - Exception');
        failures++;
    }

    // ---------- Summary ----------
    console.log('');
    if (failures === 0) {
        console.log('All tests passed');
    } else {
        console.log(failures + ' test(s) failed');
    }
    return failures;
}
