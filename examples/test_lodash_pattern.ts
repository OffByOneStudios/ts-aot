// Lodash-like pattern: IIFE returning object with many functions
const lodash = (function() {
    function runInContext() {
        function toNumber(v: any) { return +v; }
        function toFinite(v: any) { return toNumber(v); }
        function toInteger(v: any) { return toFinite(v) | 0; }
        function chunk(arr: any, size: any) {
            const s = toInteger(size);
            return [arr, s];
        }
        return { toNumber, toFinite, toInteger, chunk };
    }
    return runInContext();
})();

const num1 = lodash.toNumber(5);
const num2 = lodash.toFinite(5);
const num3 = lodash.toInteger(5);
const arr = lodash.chunk([1,2,3], 2);

console.log("toNumber(5):", num1);
console.log("toFinite(5):", num2);
console.log("toInteger(5):", num3);
console.log("chunk([1,2,3], 2):", arr);
