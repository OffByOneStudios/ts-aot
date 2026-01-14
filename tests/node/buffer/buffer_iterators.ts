// Test Buffer iterator methods: entries(), keys(), values(), toJSON()

function user_main(): number {
    console.log("=== Buffer Iterator Tests ===");

    // Test 1: keys()
    console.log("\nTest 1: keys()");
    const buf1 = Buffer.from("hello");
    const keys1 = buf1.keys();
    console.log("keys count: " + keys1.length);
    // Just use join to get all keys
    console.log("keys: " + keys1.join(", "));

    // Test 2: values()
    console.log("\nTest 2: values()");
    const buf2 = Buffer.from("ABC");
    const values2 = buf2.values();
    console.log("values count: " + values2.length);
    console.log("values: " + values2.join(", "));

    // Test 3: entries()
    console.log("\nTest 3: entries()");
    const buf3 = Buffer.from("XY");
    const entries3 = buf3.entries();
    console.log("entries count: " + entries3.length);

    // Test 4: Empty buffer
    console.log("\nTest 4: Empty buffer");
    const emptyBuf = Buffer.alloc(0);
    console.log("empty keys length: " + emptyBuf.keys().length);
    console.log("empty values length: " + emptyBuf.values().length);
    console.log("empty entries length: " + emptyBuf.entries().length);

    // Test 5: toJSON() - basic test
    console.log("\nTest 5: toJSON()");
    const buf5 = Buffer.from("Hi");
    const json5 = buf5.toJSON();
    console.log("toJSON returned something: " + (json5 !== null));

    // Test 6: Larger buffer keys/values
    console.log("\nTest 6: Larger buffer");
    const buf6 = Buffer.alloc(5);
    buf6.fill(42);  // Fill with 42 (0x2A)
    const keys6 = buf6.keys();
    const values6 = buf6.values();
    console.log("keys6 length: " + keys6.length);
    console.log("keys6: " + keys6.join(", "));
    console.log("values6 length: " + values6.length);
    console.log("values6: " + values6.join(", "));

    // Test 7: Binary data values
    console.log("\nTest 7: Binary data");
    const binBuf = Buffer.alloc(4);
    binBuf.writeUInt8(0, 0);
    binBuf.writeUInt8(127, 1);
    binBuf.writeUInt8(128, 2);
    binBuf.writeUInt8(255, 3);
    const binValues = binBuf.values();
    console.log("binary values: " + binValues.join(", "));

    console.log("\nPASS: All Buffer iterator tests completed");
    return 0;
}
