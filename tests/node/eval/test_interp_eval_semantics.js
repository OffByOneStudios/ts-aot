// EVAL-001: interpreter semantics — Annex B.3.3 block-scoped function
// declarations, destructuring binding, and new.target in eval'd code.

function user_main() {
    var failed = 0;
    function check(name, actual, expected) {
        if (actual === expected) {
            console.log("PASS: " + name);
        } else {
            console.log("FAIL: " + name + " - got " + actual + ", want " + expected);
            failed++;
        }
    }

    // Annex B.3.3: block-scoped function declarations (sloppy)
    check("b33-promote", eval("{ function f() { return 7; } } f();"), 7);
    check("b33-bare-if", eval("if (true) function g() { return 8; } g();"), 8);
    check("b33-skip-let", eval("let f = 1; { function f() {} } f;"), 1);
    check("b33-recur-block", eval("{ function r(n) { return n <= 1 ? 1 : n * r(n - 1); } } r(5);"), 120);

    // destructuring: declarators, defaults, rest, nested, params, for-of, catch
    check("destr-array", eval("var [a, b, c] = [1, 2, 3]; a + b + c;"), 6);
    check("destr-object", eval("var { x, y } = { x: 10, y: 20 }; x + y;"), 30);
    check("destr-nested", eval("var { p: [q, s] } = { p: [4, 5] }; q + s;"), 9);
    check("destr-default", eval("var [m = 7, n = 8] = [1]; m + ',' + n;"), "1,8");
    check("destr-array-rest", eval("var [h, ...tl] = [1, 2, 3, 4]; h + '/' + tl.join(',');"), "1/2,3,4");
    check("destr-param", eval("(function ({ a, b }) { return a + b; })({ a: 3, b: 4 });"), 7);
    check("destr-for-of", eval("var t = 0; for (const [k, v] of [[1, 2], [3, 4]]) t += k + v; t;"), 10);
    check("destr-catch", eval("try { throw { code: 42 }; } catch ({ code }) { code; }"), 42);

    // new.target
    check("newtarget-normal-call", eval("function F() { return new.target === undefined; } F();"), true);
    check("newtarget-construct", eval("function F() { this.t = (new.target === F); } new F().t;"), true);

    console.log("---");
    console.log("Failed: " + failed);
    return failed > 0 ? 1 : 0;
}
