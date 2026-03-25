// Test: mutual recursion between two inner function declarations
// Pattern: Express router.handle has next() calling trim_prefix() and vice versa.

function pipeline(items, transform) {
  var idx = 0;

  next();

  function next() {
    if (idx >= items.length) {
      console.log("pipeline done");
      return;
    }
    var item = items[idx++];
    process_item(item);
  }

  function process_item(item) {
    var result = transform(item);
    console.log("processed: " + result);
    next();
  }
}

pipeline(["x", "y", "z"], function(s) { return s.toUpperCase(); });

// OUTPUT: processed: X
// OUTPUT: processed: Y
// OUTPUT: processed: Z
// OUTPUT: pipeline done
