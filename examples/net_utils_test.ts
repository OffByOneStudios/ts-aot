// Test net module utilities (Milestone 100.6)

console.log("=== Testing net.isIP, net.isIPv4, net.isIPv6 ===");

// Test valid IPv4
console.log("net.isIP('127.0.0.1'):", net.isIP("127.0.0.1")); // Should be 4
console.log("net.isIPv4('127.0.0.1'):", net.isIPv4("127.0.0.1")); // Should be true
console.log("net.isIPv6('127.0.0.1'):", net.isIPv6("127.0.0.1")); // Should be false

// Test valid IPv6
console.log("net.isIP('::1'):", net.isIP("::1")); // Should be 6
console.log("net.isIPv4('::1'):", net.isIPv4("::1")); // Should be false
console.log("net.isIPv6('::1'):", net.isIPv6("::1")); // Should be true

// Test invalid
console.log("net.isIP('invalid'):", net.isIP("invalid")); // Should be 0
console.log("net.isIPv4('invalid'):", net.isIPv4("invalid")); // Should be false
console.log("net.isIPv6('invalid'):", net.isIPv6("invalid")); // Should be false

console.log("\n=== Testing auto-select family settings ===");
console.log("net.getDefaultAutoSelectFamily():", net.getDefaultAutoSelectFamily());
console.log("net.getDefaultAutoSelectFamilyAttemptTimeout():", net.getDefaultAutoSelectFamilyAttemptTimeout());

net.setDefaultAutoSelectFamily(false);
console.log("After setDefaultAutoSelectFamily(false):", net.getDefaultAutoSelectFamily());

net.setDefaultAutoSelectFamilyAttemptTimeout(500);
console.log("After setDefaultAutoSelectFamilyAttemptTimeout(500):", net.getDefaultAutoSelectFamilyAttemptTimeout());

console.log("\n=== Testing http.METHODS ===");
const methods = http.METHODS;
console.log("http.METHODS length:", methods.length);
console.log("First 5 methods:", methods.slice(0, 5).join(", "));

console.log("\n=== Testing http.STATUS_CODES ===");
// Access STATUS_CODES - this is a Map-like object
const statusCodes = http.STATUS_CODES;
console.log("http.STATUS_CODES is defined:", statusCodes !== undefined);

console.log("\n=== Testing http.maxHeaderSize ===");
console.log("http.maxHeaderSize:", http.maxHeaderSize);

console.log("\n=== Testing http.validateHeaderName/Value ===");
http.validateHeaderName("Content-Type");
console.log("http.validateHeaderName('Content-Type'): passed");

http.validateHeaderValue("Content-Type", "text/html");
console.log("http.validateHeaderValue('Content-Type', 'text/html'): passed");

console.log("\n=== All tests completed ===");
