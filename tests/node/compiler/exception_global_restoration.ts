// Regression test for the longjmp / call-frame globals corruption fix.
//
// Background: ts-aot's runtime stores the implicit `this` receiver in a
// file-static global (ts_call_this_value in TsObject.cpp) because the
// JIT-emitted ABI has no slot for it. ts_call_with_this_N saves/sets/
// restores it manually around each call. When the inner call throws via
// ts_throw (which calls longjmp), the manual restore is skipped, and the
// global keeps pointing at the aborted call's receiver. Subsequent runtime
// helpers that read `ts_get_call_this()` then see the wrong `this`.
//
// A second piece of the same bug: ts_call_with_this_N also patches
// func->context for native functions whose context is null. After a throw,
// the patched value (e.g., a boxed null from .call(null)) is left in place.
// string_proto_method used to read its receiver from `ctx` (i.e.
// func->context) and would see the leak on the next call.
//
// The fix has two parts:
//   1. Core.cpp ExceptionContext now snapshots ts_call_this_value (and
//      ts_last_call_argc) at try-block entry; ts_throw restores them
//      before longjmp, so the catch landing pad sees correct state.
//   2. string_proto_method now prefers ts_get_call_this() over its `ctx`
//      parameter (which can hold the leaked patched func->context).
//
// All scenarios below FAIL pre-fix (verified with negative-control build)
// and pass post-fix. They exercise the typed pipeline, complementing the
// .js golden_ir regression which exercises the dynamic pipeline.

function user_main(): number {
    let failures = 0;

    // 1. Receiver after a thrown null-receiver call is recovered.
    try { String.prototype.trim.call(null); } catch (e) {}
    const a = String.prototype.trim.call("  hi  ");
    if (a !== "hi") {
        console.log("FAIL:1 expected 'hi' got " + JSON.stringify(a));
        failures++;
    } else {
        console.log("PASS:1 String receiver restored after null-receiver throw");
    }

    // 2. Different aborted method, different probe.
    try { String.prototype.trim.call(undefined); } catch (e) {}
    const b = String.prototype.indexOf.call("hello", "l");
    if (b !== 2) {
        console.log("FAIL:2 expected 2 got " + b);
        failures++;
    } else {
        console.log("PASS:2 indexOf works after thrown undefined-receiver");
    }

    // 3. Catch and re-throw — outer catch must also see correct receiver.
    function rethrower(): void {
        try { String.prototype.trim.call(null); }
        catch (e) { throw e; }
    }
    try { rethrower(); } catch (e) {}
    const f = String.prototype.charAt.call("xyz", 1);
    if (f !== "y") {
        console.log("FAIL:3 expected 'y' got " + JSON.stringify(f));
        failures++;
    } else {
        console.log("PASS:3 receiver restored across re-throw across frames");
    }

    // 4. Nested try — inner aborts, outer should still see correct receiver.
    try {
        try { String.prototype.trim.call(null); } catch (e) {}
        const c = String.prototype.toUpperCase.call("abc");
        if (c !== "ABC") {
            console.log("FAIL:4 expected 'ABC' got " + JSON.stringify(c));
            failures++;
        } else {
            console.log("PASS:4 receiver restored after inner catch");
        }
    } catch (e) {
        console.log("FAIL:4 outer caught " + e);
        failures++;
    }

    if (failures === 0) {
        console.log("All exception-globals regression tests passed");
    } else {
        console.log(failures + " test(s) failed");
    }
    return failures;
}
