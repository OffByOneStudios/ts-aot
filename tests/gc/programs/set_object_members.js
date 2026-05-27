// Set with object members (movable) across GC; membership must survive.
function user_main() {
  var objs = [];
  var s = new Set();
  for (var i = 0; i < 3000; i++) {
    var o = { id: i };
    objs.push(o); s.add(o);
    var junk = { a: i, b: [i] };
  }
  __ts_gc_minor(); __ts_gc_major();
  var ok = 0;
  for (var i = 0; i < objs.length; i++) { if (s.has(objs[i]) && objs[i].id === i) ok++; }
  if (ok !== objs.length) { console.log("FAIL: " + ok + "/" + objs.length); return 1; }
  console.log("PASS"); return 0;
}
