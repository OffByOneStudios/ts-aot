// Minimal test of property access on global
console.log("typeof global:", typeof global);

// Test setting property on global directly
console.log("Setting global.testProp = 'hello'");
global.testProp = "hello";
console.log("global.testProp:", global.testProp);

// Now test with a variable
const root = global;
console.log("root:", root);
console.log("Setting root.foo = 42");
root.foo = 42;
console.log("root.foo:", root.foo);
console.log("Done!");
