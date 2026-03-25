// Test: two inner function declarations sharing outer variables
// Each must see the same closure cells for shared mutable state.
// NOTE: mutual recursion between inner funcs is a known limitation.

function pipeline(items, transform) {
  var idx = 0;

  while (idx < items.length) {
    process_item(items[idx++]);
  }
  console.log("pipeline done, processed " + idx);

  function process_item(item) {
    var result = transform(item);
    console.log("processed: " + result);
  }
}

pipeline(["x", "y", "z"], function(s) { return s.toUpperCase(); });

// OUTPUT: processed: X
// OUTPUT: processed: Y
// OUTPUT: processed: Z
// OUTPUT: pipeline done, processed 3
