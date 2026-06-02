// Closure-cell cross-gen hazard: a closure captures `box` by cell; after the
// closure and its cell tenure to old gen, the captured variable is reassigned to
// a FRESH nursery object. The cell's old->young edge must be a minor-GC root and
// be forwarded after promotion. (Closest holder to the prior closure-cell desync.)
function user_main() {
  var box = { v: 0 };
  function get() { return box; }          // captures box by cell
  function set(x) { box = x; }            // writes the cell
  __ts_gc_minor();                        // closure + cell -> old gen
  __ts_gc_minor();

  set({ v: 777, tag: "fresh", arr: [1, 2, 3] });  // cell <- fresh nursery object
  __ts_gc_minor();                        // fresh object promotes; cell must forward

  var v = __ts_gc_verify();
  if (v !== 0) { console.log("FAIL: verify reported " + v + " violations"); return 1; }
  var r = get();
  if (!r || r.v !== 777) { console.log("FAIL: r.v=" + (r && r.v)); return 1; }
  if (r.tag !== "fresh") { console.log("FAIL: r.tag=" + r.tag); return 1; }
  if (r.arr[2] !== 3) { console.log("FAIL: r.arr corrupt"); return 1; }
  console.log("PASS");
  return 0;
}
