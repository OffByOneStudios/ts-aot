// Minimal lodash-style CommonJS test
console.log("lodash_style_test.js: start");

// Mimic lodash's CommonJS detection
var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;
console.log("freeExports:", freeExports ? "truthy" : "falsy");
console.log("typeof exports:", typeof exports);

var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;
console.log("freeModule:", freeModule ? "truthy" : "falsy");
console.log("typeof module:", typeof module);

if (freeModule) {
    console.log("Inside freeModule block!");
    var _ = { version: "test" };
    (freeModule.exports = _)._ = _;
    console.log("Assigned _ to freeModule.exports");
}

console.log("lodash_style_test.js: done");
