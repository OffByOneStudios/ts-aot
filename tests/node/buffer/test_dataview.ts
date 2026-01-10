// Test DataView functionality

function user_main(): number {
    console.log("Testing DataView...");

    // Create a buffer
    const buffer = Buffer.alloc(16);

    // Create a DataView from the buffer
    const view = new DataView(buffer);

    // Test setUint8/getUint8
    view.setUint8(0, 255);
    const u8 = view.getUint8(0);
    console.log("Uint8:", u8);  // Should be 255

    // Test setInt8/getInt8
    view.setInt8(1, -128);
    const i8 = view.getInt8(1);
    console.log("Int8:", i8);  // Should be -128

    // Test setUint16/getUint16 (little endian)
    view.setUint16(2, 0x1234, true);
    const u16le = view.getUint16(2, true);
    console.log("Uint16 LE:", u16le);  // Should be 0x1234 = 4660

    // Test setUint16/getUint16 (big endian)
    view.setUint16(4, 0x5678, false);
    const u16be = view.getUint16(4, false);
    console.log("Uint16 BE:", u16be);  // Should be 0x5678 = 22136

    // Test setUint32/getUint32
    view.setUint32(8, 0x12345678, true);
    const u32 = view.getUint32(8, true);
    console.log("Uint32:", u32);  // Should be 305419896

    // Test setFloat32/getFloat32
    view.setFloat32(12, 3.14, true);
    const f32 = view.getFloat32(12, true);
    console.log("Float32:", f32);  // Should be approximately 3.14

    console.log("DataView tests complete!");
    return 0;
}
