// Many object literals held in an array across nursery overflow + forced GC.
// Holder = array element. Trigger = natural nursery overflow + forced GC.
// Differential-sensitive: corruption shows as mismatched 'ok' count vs NURSERY=0.
function user_main() {
  var n = 8000;
  var arr = [];
  for (var i = 0; i < n; i++) {
    arr.push({ id: i, name: "obj" + i, pair: [i, i + 1] });
    var junk = { x: i, y: "j" + i, z: [i] };  // nursery pressure
  }
  __ts_gc_minor();
  __ts_gc_major();
  var ok = 0;
  for (var j = 0; j < arr.length; j++) {
    var e = arr[j];
    if (e && e.id === j && e.name === "obj" + j && e.pair[0] === j && e.pair[1] === j + 1) ok++;
  }
  if (ok !== n) { console.log("FAIL: " + ok + "/" + n + " survived intact"); return 1; }
  console.log("PASS");
  return 0;
}
