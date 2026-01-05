// Test 105.11.5.2: Object Property Assignment
// Pattern: Dynamic property assignment on objects

var obj = {};
obj.x = 10;
obj.y = 20;
obj['z'] = 30;
console.log("x:", obj.x);
console.log("y:", obj.y);
console.log("z:", obj.z);
console.log("PASS: object_assign");
