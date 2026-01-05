// Test IIFE returning object with functions
const obj: any = (function() {
    function double(x: any) { return x * 2; }
    return { double };
})();

const result = obj.double(5);
console.log("obj.double(5):", result);  // Should be 10
