// Many functions test
const result = (function() {
    function f1() { return 1; }
    function f2() { return f1() + 1; }
    function f3() { return f2() + 1; }
    function f4() { return f3() + 1; }
    function f5() { return f4() + 1; }
    function f6() { return f5() + 1; }
    function f7() { return f6() + 1; }
    function f8() { return f7() + 1; }
    function f9() { return f8() + 1; }
    function f10() { return f9() + 1; }
    return f10();
})();

console.log("result:", result);  // Should be 10
