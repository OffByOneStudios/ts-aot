// Test: function expression with inner function declaration capturing outer vars
// The inner function must see the function expression's scope variables.

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
