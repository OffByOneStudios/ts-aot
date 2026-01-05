// Test the lodash pattern
const freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
console.log("freeGlobal:", freeGlobal);
console.log("typeof freeGlobal:", typeof freeGlobal);

const root = freeGlobal || null;
console.log("root:", root);
console.log("typeof root:", typeof root);

// Try to set a property on root
if (root) {
    console.log("Setting root.test = 42");
    root.test = 42;
    console.log("root.test:", root.test);
}
