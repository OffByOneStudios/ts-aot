// Test for net.SocketAddress and net.BlockList

// Test SocketAddress
console.log("=== SocketAddress Tests ===");
const addr = new net.SocketAddress();
console.log("Default address:", addr.address);
console.log("Default family:", addr.family);
console.log("Default port:", addr.port);
console.log("Default flowlabel:", addr.flowlabel);

// Test SocketAddress.parse
const parsed = net.SocketAddress.parse("192.168.1.1:8080");
if (parsed) {
    console.log("Parsed address:", parsed.address);
    console.log("Parsed port:", parsed.port);
    console.log("Parsed family:", parsed.family);
} else {
    console.log("Parse returned undefined");
}

// Test BlockList
console.log("\n=== BlockList Tests ===");
const blockList = new net.BlockList();

// Check if it's a BlockList
console.log("Is BlockList:", net.BlockList.isBlockList(blockList));
console.log("Is BlockList (string):", net.BlockList.isBlockList("not a blocklist"));

// Add some addresses
blockList.addAddress("123.123.123.123");
blockList.addRange("10.0.0.1", "10.0.0.10");
blockList.addSubnet("192.168.0.0", 24);

// Check blocked addresses
console.log("Check 123.123.123.123:", blockList.check("123.123.123.123"));
console.log("Check 10.0.0.5:", blockList.check("10.0.0.5"));
console.log("Check 10.0.0.50:", blockList.check("10.0.0.50"));  // Should be false
console.log("Check 192.168.0.100:", blockList.check("192.168.0.100"));
console.log("Check 192.168.1.1:", blockList.check("192.168.1.1"));  // Should be false

// Get rules
const rules = blockList.rules;
console.log("Number of rules:", rules.length);

console.log("\n=== All Tests Complete ===");
