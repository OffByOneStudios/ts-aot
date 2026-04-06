// RUN: %ts-aot %s -o %t.exe && %t.exe
// OUTPUT: pass:1
// OUTPUT: pass:2
// OUTPUT: pass:3
// OUTPUT: pass:4
// OUTPUT: pass:5
// OUTPUT: pass:6

// Regression for the longjmp / ts_call_this_value corruption fix.
// Before the fix, an exception thrown across String.prototype.X.call(...)
// or fn.call(thisArg) would leave the runtime's `this` global pointing at
// the aborted call's receiver, silently breaking the next runtime helper
// invocation in the same process. ts_throw now snapshot/restores the
// calling-convention globals via ExceptionContext (Core.cpp).

function user_main() {
    // 1. Receiver after a thrown null-receiver call is recovered.
    try { String.prototype.trim.call(null); } catch (e) {}
    var a = String.prototype.trim.call("  hi  ");
    console.log(a === "hi" ? "pass:1" : "FAIL:1 got " + JSON.stringify(a));

    // 2. Different aborted method, different probe.
    try { String.prototype.trim.call(undefined); } catch (e) {}
    var b = String.prototype.indexOf.call("hello", "l");
    console.log(b === 2 ? "pass:2" : "FAIL:2 got " + b);

    // 3. Nested try/catch — inner aborts, outer should still see correct this.
    try {
        try { String.prototype.trim.call(null); } catch (e) {}
        var c = String.prototype.toUpperCase.call("abc");
        console.log(c === "ABC" ? "pass:3" : "FAIL:3 got " + JSON.stringify(c));
    } catch (e) {
        console.log("FAIL:3 outer caught " + e);
    }

    // 4. User function with explicit `this` throws; next .call works.
    function thrower() { throw new Error("boom"); }
    try { thrower.call({tag: "bad"}); } catch (e) {}
    function reader() { return this.tag; }
    var d = reader.call({tag: "good"});
    console.log(d === "good" ? "pass:4" : "FAIL:4 got " + JSON.stringify(d));

    // 5. Catch and re-throw — outer catch must also see correct this.
    function rethrower() {
        try { String.prototype.trim.call(null); }
        catch (e) { throw e; }
    }
    try { rethrower(); } catch (e) {}
    var f = String.prototype.charAt.call("xyz", 1);
    console.log(f === "y" ? "pass:5" : "FAIL:5 got " + JSON.stringify(f));

    // 6. Throw from inside a method invoked via .call, then check sibling.
    var obj = {
        boom: function () { throw new Error("inner"); },
        name: function () { return "ok"; }
    };
    try { obj.boom.call(obj); } catch (e) {}
    var g = obj.name.call(obj);
    console.log(g === "ok" ? "pass:6" : "FAIL:6 got " + JSON.stringify(g));

    return 0;
}
