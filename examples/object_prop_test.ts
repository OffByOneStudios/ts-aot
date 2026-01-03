// Minimal test for object property assignment crash
const obj: any = {};

console.log("Test 1: Simple property set");
obj.foo = 42;
console.log("obj.foo =", obj.foo);

console.log("\nTest 2: Bracket notation");
obj["bar"] = "hello";
console.log("obj['bar'] =", obj["bar"]);

console.log("\nTest 3: Boolean value");
obj["flag"] = true;
console.log("obj['flag'] =", obj["flag"]);

console.log("\nTest 4: Tag-like key");
obj["[object Array]"] = true;
console.log("obj['[object Array]'] =", obj["[object Array]"]);

console.log("\nAll tests passed!");
