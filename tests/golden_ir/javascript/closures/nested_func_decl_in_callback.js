// Test: inner function declaration called from different contexts
// The inner function must access outer variables regardless of call site.

function withHelper(items, cb) {
  var count = items.length;

  function getCount() {
    return count;
  }

  cb(getCount);
}

withHelper(["a", "b", "c"], function(getCount) {
  console.log("count from callback: " + getCount());
});

// OUTPUT: count from callback: 3
