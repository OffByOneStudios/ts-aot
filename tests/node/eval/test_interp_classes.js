// EVAL-001: class declarations and expressions evaluated by the interpreter —
// methods, accessors, static members, instance fields, extends + super.

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

    check("class-instanceof", eval("class C {} new C() instanceof C;"), true);
    check("class-method", eval("class C { m() { return 5; } } new C().m();"), 5);
    check("class-ctor-field", eval("class C { constructor(x) { this.x = x; } } new C(7).x;"), 7);
    check("class-getter", eval("class C { get v() { return 9; } } new C().v;"), 9);
    check("class-static", eval("class C { static s() { return 3; } } C.s();"), 3);
    check("class-field-init", eval("class C { x = 42; } new C().x;"), 42);
    check("class-expr", eval("var K = class { m() { return 8; } }; new K().m();"), 8);
    check("class-extends", eval("class A { m() { return 1; } } class B extends A {} new B().m();"), 1);
    check("class-super-call", eval("class A { constructor() { this.a = 1; } } class B extends A { constructor() { super(); this.b = 2; } } var o = new B(); o.a + o.b;"), 3);
    check("class-super-method", eval("class A { m() { return 10; } } class B extends A { m() { return super.m() + 5; } } new B().m();"), 15);
    check("class-derived-newtarget", eval("class A { constructor() { this.nt = new.target; } } class B extends A {} (new B().nt === B);"), true);
    check("class-lexical-no-leak", (function () { eval("class Hidden {}"); return typeof Hidden; })(), "undefined");
    check("class-completion", eval("1; class C {}"), 1);

    console.log("---");
    console.log("Failed: " + failed);
    return failed > 0 ? 1 : 0;
}
