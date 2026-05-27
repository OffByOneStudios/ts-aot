// TsArray + element buffer (movable) held across GC pressure + forced GC.
function user_main() {
  var rows = [];
  for (var i = 0; i < 4000; i++) {
    var a = [];
    for (var j = 0; j < 8; j++) a.push(i * 8 + j);
    rows.push(a);
    var junk = [i, i + 1, "s" + i];
  }
  __ts_gc_minor(); __ts_gc_major();
  var ok = 0;
  for (var i = 0; i < rows.length; i++) {
    if (rows[i].length === 8 && rows[i][0] === i * 8 && rows[i][7] === i * 8 + 7) ok++;
  }
  if (ok !== rows.length) { console.log("FAIL: " + ok + "/" + rows.length); return 1; }
  console.log("PASS"); return 0;
}
