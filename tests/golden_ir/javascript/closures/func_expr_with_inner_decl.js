// Test: function expression containing an inner function declaration
// The inner function must capture variables from the function expression's scope.

var obj = {};

obj.run = function run(items) {
  var processed = 0;

  for (var i = 0; i < items.length; i++) {
    handle(items[i]);
  }
  console.log("total processed: " + processed);

  function handle(item) {
    console.log("handle: " + item);
    processed++;
  }
};

obj.run(["one", "two", "three"]);

// OUTPUT: handle: one
// OUTPUT: handle: two
// OUTPUT: handle: three
// OUTPUT: total processed: 3
