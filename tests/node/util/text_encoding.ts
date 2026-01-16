// Test TextEncoder and TextDecoder

function user_main(): number {
    console.log("Testing TextEncoder and TextDecoder");

    // Test TextEncoder
    const encoder = new TextEncoder();
    console.log("TextEncoder encoding: " + encoder.encoding);

    // Encode a simple string
    const encoded = encoder.encode("Hello, World!");
    console.log("Encoded length: " + encoded.length);

    // Test TextDecoder
    const decoder = new TextDecoder();
    console.log("TextDecoder encoding: " + decoder.encoding);

    // Decode the buffer back to string
    const decoded = decoder.decode(encoded);
    console.log("Decoded: " + decoded);

    // Test with empty string
    const emptyEncoded = encoder.encode("");
    console.log("Empty encoded length: " + emptyEncoded.length);

    // Test UTF-8 multi-byte characters
    const utf8Encoder = new TextEncoder();
    const utf8Data = utf8Encoder.encode("Hello");
    console.log("UTF-8 'Hello' bytes: " + utf8Data.length);

    // Test TextDecoder with options
    const decoder2 = new TextDecoder("utf-8");
    console.log("Decoder2 encoding: " + decoder2.encoding);

    // Verify roundtrip
    const original = "Test string 123";
    const roundtrip = decoder.decode(encoder.encode(original));
    if (roundtrip === original) {
        console.log("Roundtrip: PASS");
    } else {
        console.log("Roundtrip: FAIL");
        console.log("Expected: " + original);
        console.log("Got: " + roundtrip);
    }

    console.log("TextEncoder/TextDecoder tests complete");
    return 0;
}
