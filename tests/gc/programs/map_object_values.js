// Map with object-literal values (old-gen bucket -> nursery edge: exercises the
// Phase-3 full old-gen scan / write barrier). Holder = Map value. Trigger = GC.
function user_main() {
  var m = new Map();
  var n = 3000;
  for (var i = 0; i < n; i++) {
    m.set("k" + i, { id: i, payload: [i, i + 1, i + 2] });
    var junk = { a: i, b: "j" + i };
  }
  __ts_gc_minor();
  __ts_gc_major();
  var ok = 0;
  for (var i = 0; i < n; i++) {
    var v = m.get("k" + i);
    if (v && v.id === i && v.payload[2] === i + 2) ok++;
  }
  if (ok !== n) { console.log("FAIL: " + ok + "/" + n + " map values intact"); return 1; }
  console.log("PASS");
  return 0;
}
