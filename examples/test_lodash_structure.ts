// Test that mimics lodash's actual structure more closely

// Simulate the lodash IIFE structure
const _ = (function() {
    function runInContext() {
        function lodash(value: any) {
            return value;
        }
        lodash.chunk = function(arr: any[], size: number) {
            return [arr];
        };
        return lodash;
    }
    
    // This is what lodash does at line 17182
    const _ = runInContext();
    
    // Check the value before export
    console.log('typeof _ before export:', typeof _);
    console.log('typeof _.chunk before export:', typeof _.chunk);
    
    // Return it so we can test
    return _;
})();

// Now test exporting it
const freeModule = { exports: {} };
const freeExports = freeModule.exports;

console.log('=== Before Assignment ===');
console.log('typeof _:', typeof _);
console.log('typeof _.chunk:', typeof _.chunk);

// This is what lodash does at line 17201
(freeModule.exports = _)._ = _;

console.log('\n=== After Assignment ===');
console.log('typeof freeModule.exports:', typeof freeModule.exports);
console.log('typeof freeModule.exports.chunk:', typeof (freeModule.exports as any).chunk);
console.log('typeof (freeModule.exports as any)._:', typeof (freeModule.exports as any)._);
