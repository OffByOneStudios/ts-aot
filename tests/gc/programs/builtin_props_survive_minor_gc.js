// Minimal, deterministic repro of the central moving-GC corruption: builtin
// objects (Object, Object.prototype, the Array binding, ...) lose their
// property-map contents after a SINGLE minor GC.
//
// One forced minor GC is enough — much smaller than cyclical_clone_deep.js.
//
// IMPORTANT: the repro is sensitive to conservative stack pinning. If a
// reference to the builtin map happens to sit in a stack local across the GC,
// it gets pinned and survives, masking the bug. So this test deliberately
// does NOT hold any pre-GC result in a local across __ts_gc_minor(); it
// resolves Object.prototype.toString fresh AFTER the GC.
//
// Observed today (commit 876b04e, mark-symmetry fix landed):
//   before GC: Object.prototype.toString.call([]) == "[object Array]"
//   after  GC: Object.prototype.toString.call([]) == "undefined"   <-- BUG
//   The toString FUNCTION survives (a saved local copy still works) and
//   compiler intrinsics (Object.keys(x), Math.floor(x)) still work — only the
//   runtime property-map lookups on builtins are corrupted. So the minor GC
//   clobbers the builtin objects' backing property storage (TsMap/TsHashTable).
//   The global object graph is reachable only via `extern "C" TsValue* global`
//   (TsObject.cpp:9386), a .data-segment pointer the conservative scan does NOT
//   cover (it scans the stack), with no ts_gc_register_root — so builtin maps
//   created in the nursery at init are not reliably marked/forwarded across a
//   minor GC.
//
//   EXPECTED today: default nursery FAILS, TS_GC_NURSERY=0 PASSES (differential).
//   When fixed, all configs print PASS and this moves back into programs/.

function user_main() {
  // Establish the pre-GC behavior inline (no result held across the GC).
  if (Object.prototype.toString.call([]) !== "[object Array]") {
    console.log("FAIL: pre-GC baseline wrong"); return 1;
  }

  __ts_gc_minor();

  // Resolve everything fresh after the GC.
  var tag = Object.prototype.toString.call([]);
  if (tag !== "[object Array]") {
    console.log("FAIL: post-GC Object.prototype.toString.call([]) = " + tag);
    return 1;
  }
  if (Object.prototype.toString.call({}) !== "[object Object]") {
    console.log("FAIL: post-GC toString.call({}) corrupt"); return 1;
  }
  console.log("PASS");
  return 0;
}
