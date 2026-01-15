// Test Buffer.from(buffer) for buffer copying

function user_main(): number {
    console.log("Testing Buffer.from(buffer):");

    // Create original buffer
    const original: Buffer = Buffer.from("Hello, World!");
    console.log("Original buffer length:", original.length);
    console.log("Original content:", original.toString());

    // Copy the buffer using Buffer.from(buffer)
    const copy: Buffer = Buffer.from(original);
    console.log("Copy buffer length:", copy.length);
    console.log("Copy content:", copy.toString());

    // Verify they have the same length
    if (original.length !== copy.length) {
        console.log("FAIL: Lengths differ");
        return 1;
    }
    console.log("PASS: Lengths match");

    // Verify they have the same content
    if (original.toString() !== copy.toString()) {
        console.log("FAIL: Contents differ");
        return 1;
    }
    console.log("PASS: Contents match");

    // Verify they are independent (modifying one doesn't affect the other)
    copy.fill(65); // Fill with 'A'
    if (original.toString() === copy.toString()) {
        console.log("FAIL: Buffers share underlying data");
        return 1;
    }
    console.log("PASS: Buffers are independent");
    console.log("Original after copy modified:", original.toString());
    console.log("Copy after fill:", copy.toString());

    console.log("All tests passed!");
    return 0;
}
