// Test string_decoder module basic functionality
import { StringDecoder } from 'string_decoder';

function user_main(): number {
    console.log("Testing string_decoder module...");

    // Test basic UTF-8 decoding
    console.log("Testing basic UTF-8 decoding...");
    const decoder = new StringDecoder('utf8');

    // Create a simple ASCII buffer
    const buf1 = Buffer.from("Hello");
    const result1 = decoder.write(buf1);
    console.log("  ASCII: " + result1);

    // Create another buffer
    const buf2 = Buffer.from(" World");
    const result2 = decoder.write(buf2);
    console.log("  Result: " + result2);

    // Test end()
    const final = decoder.end();
    console.log("  End: " + (final.length === 0 ? "empty (correct)" : final));

    // Test multi-byte character handling
    console.log("Testing multi-byte characters...");
    const decoder2 = new StringDecoder('utf8');

    // UTF-8 encoding of 你 (U+4F60) is E4 BD A0
    const chineseChar = Buffer.alloc(3);
    chineseChar.writeUInt8(0xE4, 0);
    chineseChar.writeUInt8(0xBD, 1);
    chineseChar.writeUInt8(0xA0, 2);

    const chineseResult = decoder2.write(chineseChar);
    console.log("  Chinese char: " + chineseResult);

    // Test default encoding
    console.log("Testing default encoding...");
    const decoder3 = new StringDecoder();
    const buf3 = Buffer.from("Default");
    const result3 = decoder3.write(buf3);
    console.log("  Default encoding: " + result3);

    console.log("All string_decoder tests passed!");
    return 0;
}
