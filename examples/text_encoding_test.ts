// Simple test for TextEncoder/TextDecoder
console.log("Testing TextEncoder...");

const encoder = new TextEncoder();
console.log("Created encoder");
console.log("Encoding:", encoder.encoding);

// Note: encode returns a Buffer, not a Uint8Array in ts-aot
const data = encoder.encode("Hello");
console.log("Encoded data exists:", data !== undefined);

const decoder = new TextDecoder();
console.log("Created decoder");

const decoded = decoder.decode(data);
console.log("Decoded:", decoded);

console.log("Done!");
