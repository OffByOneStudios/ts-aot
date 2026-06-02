// Generational write-barrier hazard via array element: an OLD-gen array receives
// FRESH nursery objects after tenuring, then a minor GC promotes them. Each array
// slot holding an old->young edge must be a minor-GC root and be forwarded.
// Holder = old-gen array element. Trigger = minor GC after cross-gen pushes.
function user_main() {
  var arr = [];
  for (var i = 0; i < 3; i++) arr.push({ seed: i });  // initial members
  __ts_gc_minor();   // arr + members survive -> old gen
  __ts_gc_minor();   // ensure tenured

  // Cross-generational stores: old-gen array <- fresh nursery objects.
  for (var j = 0; j < 8; j++) arr.push({ value: 1000 + j, label: "n" + j });

  __ts_gc_minor();   // fresh members promote; array slots must be forwarded

  var v = __ts_gc_verify();
  if (v !== 0) { console.log("FAIL: verify reported " + v + " violations"); return 1; }
  if (arr.length !== 11) { console.log("FAIL: length=" + arr.length); return 1; }
  for (var k = 0; k < 8; k++) {
    var e = arr[3 + k];
    if (!e || e.value !== 1000 + k) { console.log("FAIL: arr[" + (3 + k) + "].value=" + (e && e.value)); return 1; }
    if (e.label !== "n" + k) { console.log("FAIL: arr[" + (3 + k) + "].label=" + e.label); return 1; }
  }
  console.log("PASS");
  return 0;
}
