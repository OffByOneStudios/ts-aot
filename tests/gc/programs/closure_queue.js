// Faithful shape of the lodash qunit harness: a closure-captured queue of
// {id,name,ctx,fn} object literals, registered under nursery pressure, then
// executed via a nested runAll() that reads each entry and invokes its closure.
// This is the workload class that exhibited the systemic corruption at scale.
function user_main() {
  var queue = [];
  var totalOk = 0;
  function register(i) {
    queue.push({ id: i, name: "t" + i, ctx: {}, fn: function(a) { return a.want === i ? i : -1; } });
  }
  function runAll() {
    for (var i = 0; i < queue.length; i++) {
      var t = queue[i];
      var assert = { want: t.id };
      if (typeof t.fn === "function" && t.fn.call(t.ctx, assert) === t.id) totalOk++;
    }
  }
  for (var i = 0; i < 8000; i++) {
    register(i);
    var junk = { a: i, b: "s" + i, c: [i, i] };
  }
  runAll();
  if (totalOk !== queue.length) { console.log("FAIL: ran " + totalOk + "/" + queue.length); return 1; }
  console.log("PASS");
  return 0;
}
