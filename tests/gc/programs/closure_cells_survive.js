// Closures capturing mutable cells must survive GC (BUG 4 regression guard:
// closures/cells are tenured to old-gen). Holder = array of closures + their
// shared cells. Trigger = GC pressure during creation.
function user_main() {
  var counters = [];
  for (var i = 0; i < 5000; i++) {
    (function(start) {
      var count = start;
      counters.push(function() { return ++count; });
    })(i * 10);
    var junk = { a: i, b: [i] };
  }
  __ts_gc_minor();
  __ts_gc_major();
  var ok = 0;
  for (var j = 0; j < counters.length; j++) {
    if (typeof counters[j] === "function" && counters[j]() === j * 10 + 1) ok++;
  }
  if (ok !== counters.length) { console.log("FAIL: " + ok + "/" + counters.length + " closures intact"); return 1; }
  console.log("PASS");
  return 0;
}
