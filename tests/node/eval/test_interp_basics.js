// EVAL-001: tree-walking interpreter — basic eval() and Function() coverage.
// Each check reports a failure; user_main returns non-zero so the node gate
// flags any regression in the interpreter subsystem.

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

    // indirect eval: expressions and statements
    check("eval-arith", eval("1 + 2 * 3"), 7);
    check("eval-string", eval("'a' + 'b' + 'c'"), "abc");
    check("eval-var-let", eval("let x = 10; x * 4;"), 40);
    check("eval-if", eval("var r; if (2 > 1) r = 'yes'; else r = 'no'; r;"), "yes");
    check("eval-for", eval("var s = 0; for (var i = 0; i < 5; i++) s += i; s;"), 10);
    check("eval-while", eval("var n = 1, c = 0; while (n < 100) { n *= 2; c++; } c;"), 7);
    check("eval-fn-decl", eval("function sq(a) { return a * a; } sq(6);"), 36);
    check("eval-closure", eval("function mk() { var k = 0; return function () { return ++k; }; } var f = mk(); f(); f();"), 2);
    check("eval-completion", eval("1; 2; 3;"), 3);
    check("eval-ternary", eval("var v = 5; v > 3 ? 'big' : 'small';"), "big");

    // Function(params..., body) constructor
    var add = Function("a", "b", "return a + b;");
    check("fn-ctor-2args", add(4, 5), 9);
    var noArgs = Function("return 42;");
    check("fn-ctor-0args", noArgs(), 42);
    var usesClosureArg = Function("x", "return function () { return x * 10; };");
    check("fn-ctor-returns-fn", usesClosureArg(3)(), 30);
    var viaArray = Function(["p", "q"], "return p - q;");
    check("fn-ctor-array-params", viaArray(10, 3), 7);

    // direct eval inside interpreted (eval'd) code sees the local scope
    check("direct-eval-scope", eval("var local = 99; eval('local + 1');"), 100);

    // try/catch inside eval
    check("eval-try-catch", eval("var out; try { throw new Error('boom'); } catch (e) { out = e.message; } out;"), "boom");
    check("eval-optional-catch", eval("var ok = false; try { throw 1; } catch { ok = true; } ok;"), true);

    console.log("---");
    console.log("Failed: " + failed);
    return failed > 0 ? 1 : 0;
}
