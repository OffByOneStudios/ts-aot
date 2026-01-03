// Test IIFE-in-IIFE pattern like lodash's nodeUtil
// This pattern: var x = (function() { try { ... } catch(e) {} }());

var outer = {};

// Simple IIFE that should return undefined
var inner = (function() {
    try {
        // Try to access something that doesn't exist
        var x = outer.foo.bar;
        return x;
    } catch (e) {
        // Should catch the error
        console.log("Caught error in inner IIFE");
    }
})();

console.log("inner value:", inner);

// Set something on outer
outer.test = "success";
console.log("outer.test:", outer.test);

// Export something
module.exports = {
    outer: outer,
    inner: inner
};
