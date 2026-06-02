// Generational write-barrier hazard: an OLD-gen object's field is assigned a
// FRESH nursery object, then a minor GC promotes the child. The old->young edge
// must be a minor-GC root (card table) and root.child must be forwarded to the
// child's promoted address. If the write barrier is missing, root.child is stale.
// Holder = old-gen object inline field. Trigger = minor GC after cross-gen store.
function user_main() {
  var root = { child: null, tag: "root" };
  __ts_gc_minor();   // root survives the local scan -> promoted to old gen
  __ts_gc_minor();   // ensure tenured

  // Cross-generational store: old-gen root . field <- fresh nursery object.
  root.child = { value: 98765, name: "freshchild", arr: [7, 8, 9] };

  __ts_gc_minor();   // child promotes; root.child must be forwarded

  var v = __ts_gc_verify();
  if (v !== 0) { console.log("FAIL: verify reported " + v + " violations"); return 1; }
  if (root.child === null) { console.log("FAIL: child lost (null)"); return 1; }
  if (root.child.value !== 98765) { console.log("FAIL: child.value=" + root.child.value); return 1; }
  if (root.child.name !== "freshchild") { console.log("FAIL: child.name=" + root.child.name); return 1; }
  if (root.child.arr[2] !== 9) { console.log("FAIL: child.arr corrupt"); return 1; }
  console.log("PASS");
  return 0;
}
