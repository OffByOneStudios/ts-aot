// Test case for inline boxing with Map element access

const map = new Map<string, number>();
map.set("a", 1);
map.set("b", 2);
map.set("c", 3);

// Test 1: Direct Map access
console.log("Test 1: Direct Map.get");
console.log("map.get('a') = " + map.get("a"));
console.log("map.get('b') = " + map.get("b"));

// Test 2: Computed key access (if supported)
console.log("\nTest 2: Computed key access");
const key = "c";
console.log("map.get(key) = " + map.get(key));

// Test 3: Element access syntax (if supported)
// Note: TypeScript Map doesn't support map[key] syntax directly
// This test is for runtime Map-like objects

console.log("\nDone");
