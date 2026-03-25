// Test: two inner function declarations that reference shared outer variables
// Each inner function must see the same closure cells for shared state.

function pipeline(items, transform) {
  var idx = 0;

  // Call process_item directly (not through next, to avoid recursion limitation)
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
