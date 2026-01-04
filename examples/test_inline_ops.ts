// Comprehensive test of inline IR operations
console.log("=== Testing Inline IR Operations ===");

// Test Map operations
console.log("\n--- Map Operations ---");
const map = new Map<string, number>();
map.set("foo", 42);
map.set("bar", 123);
const v1 = map.get("foo");
const v2 = map.get("bar");
console.log("Map.get('foo'):", v1);
console.log("Map.get('bar'):", v2);

// Test Array operations
console.log("\n--- Array Operations ---");
const arr = [10, 20, 30];
console.log("arr[0]:", arr[0]);
console.log("arr[1]:", arr[1]);
arr[1] = 99;
console.log("arr[1] after set:", arr[1]);

// Test Object operations
console.log("\n--- Object Operations ---");
const obj = { x: 5, y: 10 };
console.log("obj.x:", obj.x);
console.log("obj.y:", obj.y);
obj.x = 15;
console.log("obj.x after set:", obj.x);

// Test computed property access
console.log("\n--- Computed Property Access ---");
const key = "y";
console.log("obj[key]:", obj[key]);

console.log("\n=== All Tests Complete ===");
