// Dynamically-built strings (TsString headers movable) kept in an array.
function user_main() {
  var strs = [];
  for (var i = 0; i < 6000; i++) {
    strs.push(("val-" + i + "-" + (i * 7)).toUpperCase());
    var junk = { a: i, b: "x" + i };
  }
  __ts_gc_minor(); __ts_gc_major();
  var ok = 0;
  for (var i = 0; i < strs.length; i++) {
    if (strs[i] === ("val-" + i + "-" + (i * 7)).toUpperCase()) ok++;
  }
  if (ok !== strs.length) { console.log("FAIL: " + ok + "/" + strs.length); return 1; }
  console.log("PASS"); return 0;
}
