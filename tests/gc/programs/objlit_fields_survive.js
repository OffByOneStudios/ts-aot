// Core: an object literal's fields survive a forced minor + major GC.
// Holder = stack local. Trigger = forced GC builtins.
function user_main() {
  var obj = { a: 1, b: 2, s: "hello", arr: [10, 20, 30] };
  __ts_gc_minor();
  __ts_gc_major();
  var v = __ts_gc_verify();
  if (v !== 0) { console.log("FAIL: verify reported " + v + " violations"); return 1; }
  if (obj.a !== 1 || obj.b !== 2) { console.log("FAIL: a=" + obj.a + " b=" + obj.b); return 1; }
  if (obj.s !== "hello") { console.log("FAIL: s=" + obj.s); return 1; }
  if (obj.arr[0] !== 10 || obj.arr[2] !== 30) { console.log("FAIL: arr corrupt"); return 1; }
  console.log("PASS");
  return 0;
}
