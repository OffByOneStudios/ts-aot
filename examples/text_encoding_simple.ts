// Simple TextEncoder test
console.log("Test 1: Create encoder");
const encoder = new TextEncoder();
console.log("Test 2: Check encoding property");
console.log(encoder.encoding);

console.log("Test 3: Encode string");
const data = encoder.encode("ABC");

console.log("Test 4: Create decoder");
const decoder = new TextDecoder();

console.log("Test 5: Decode");
const result = decoder.decode(data);
console.log("Result:");
console.log(result);
console.log("Done");
