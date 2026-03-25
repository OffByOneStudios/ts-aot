// Test: closure captures that are mutated by both inner and outer function
// The shared cell must reflect mutations from both sides.

function makeIterator(items) {
  var pos = 0;

  function advance() {
    if (pos >= items.length) return null;
    return items[pos++];
  }

  function reset() {
    pos = 0;
  }

  return { advance: advance, reset: reset, getPos: function() { return pos; } };
}

var it = makeIterator(["a", "b", "c"]);
console.log(it.advance());
console.log(it.advance());
console.log("pos=" + it.getPos());
it.reset();
console.log("after reset, pos=" + it.getPos());
console.log(it.advance());

// OUTPUT: a
// OUTPUT: b
// OUTPUT: pos=2
// OUTPUT: after reset, pos=0
// OUTPUT: a
