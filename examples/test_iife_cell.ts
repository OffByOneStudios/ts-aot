// IIFE pattern like lodash
const result = (function() {
    function inner1() { return 1; }
    function inner2() { return inner1() + 1; }
    
    console.log("inner: typeof inner1:", typeof inner1);
    console.log("inner: typeof inner2:", typeof inner2);
    
    return {
        a: inner1,
        b: inner2
    };
})();

console.log("after IIFE, typeof result:", typeof result);
console.log("typeof result.a:", typeof result.a);
console.log("typeof result.b:", typeof result.b);
console.log("result.a():", result.a());
console.log("result.b():", result.b());
