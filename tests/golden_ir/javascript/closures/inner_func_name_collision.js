// Test: two functions with identically-named inner function declarations
// Each inner `helper()` must be a separate compiled function with its own captures.
// Bug fixed: both compiled to @helper, corrupting capture layouts.

function taskA(items) {
  var label = "A";

  helper();

  function helper() {
    console.log(label + ": " + items.join(","));
  }
}

function taskB(values) {
  var label = "B";

  helper();

  function helper() {
    console.log(label + ": " + values.join(";"));
  }
}

taskA(["x", "y"]);
taskB(["1", "2"]);

// OUTPUT: A: x,y
// OUTPUT: B: 1;2
