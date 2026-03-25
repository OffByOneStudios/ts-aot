// Test: inner function declaration captures variables from outer function
// The inner function is hoisted and must see outer variables via closure cells.
// NOTE: recursive self-call from inner function is a known limitation.
// This test verifies the first call works (capture is correct).

function dispatch(items) {
  var idx = 0;
  var count = items.length;

  next();
  console.log("dispatch returned, idx=" + idx);

  function next() {
    if (idx >= count) {
      return;
    }
    var item = items[idx++];
    console.log("item: " + item);
  }
}

dispatch(["a", "b", "c"]);

// OUTPUT: item: a
// OUTPUT: dispatch returned, idx=1
