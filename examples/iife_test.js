// Test IIFE access to module scope variables
console.log("iife_test.js: start");

// This is the structure lodash uses:
;(function() {
    console.log("Inside IIFE");
    console.log("typeof exports:", typeof exports);
    console.log("typeof module:", typeof module);
    
    var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;
    console.log("freeExports:", freeExports ? "truthy" : "falsy");
    
    var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;
    console.log("freeModule:", freeModule ? "truthy" : "falsy");
    
    if (freeModule) {
        console.log("Setting freeModule.exports = test object");
        freeModule.exports = { version: "1.0.0" };
    } else {
        console.log("freeModule is falsy, exports NOT set");
    }
    
    console.log("IIFE done");
}.call(this));

console.log("iife_test.js: done");
