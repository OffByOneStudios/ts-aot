// Deep field chain a.b.c.d.e must survive GC with internal pointers forwarded.
// Holder = object inline field chain. Trigger = forced GC + pressure.
function user_main() {
  var root = { b: { c: { d: { e: { value: 12345, tag: "leaf" } } } } };
  for (var i = 0; i < 4000; i++) { var junk = { p: i, q: [i, i] }; }
  __ts_gc_minor();
  __ts_gc_major();
  if (root.b.c.d.e.value !== 12345) { console.log("FAIL: deep value=" + root.b.c.d.e.value); return 1; }
  if (root.b.c.d.e.tag !== "leaf") { console.log("FAIL: deep tag=" + root.b.c.d.e.tag); return 1; }
  console.log("PASS");
  return 0;
}
