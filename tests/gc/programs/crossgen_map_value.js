// Generational write-barrier hazard via Map value: an OLD-gen Map receives FRESH
// nursery object values after tenuring, then a minor GC promotes them. The Map's
// internal value slots holding old->young edges must be minor-GC roots + forwarded.
// Holder = old-gen Map value slot. Trigger = minor GC after cross-gen sets.
function user_main() {
  var m = new Map();
  m.set("seed", { s: 0 });
  __ts_gc_minor();   // map + entry survive -> old gen
  __ts_gc_minor();   // ensure tenured

  // Cross-generational stores: old-gen map <- fresh nursery object values.
  for (var i = 0; i < 6; i++) m.set("k" + i, { value: 500 + i, name: "v" + i });

  __ts_gc_minor();   // fresh values promote; map slots must be forwarded

  var v = __ts_gc_verify();
  if (v !== 0) { console.log("FAIL: verify reported " + v + " violations"); return 1; }
  for (var j = 0; j < 6; j++) {
    var e = m.get("k" + j);
    if (!e || e.value !== 500 + j) { console.log("FAIL: m[k" + j + "].value=" + (e && e.value)); return 1; }
    if (e.name !== "v" + j) { console.log("FAIL: m[k" + j + "].name=" + e.name); return 1; }
  }
  console.log("PASS");
  return 0;
}
