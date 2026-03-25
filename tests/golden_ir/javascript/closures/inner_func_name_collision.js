// Test: two functions with identically-named inner function declarations
// that call themselves recursively. Each must be a separate compiled function
// with its own captures AND working self-reference.

function handlerA(items) {
  var prefix = "A";
  var idx = 0;

  next();

  function next() {
    if (idx >= items.length) return;
    console.log(prefix + ": " + items[idx++]);
    next();
  }
}

function handlerB(values) {
  var prefix = "B";
  var idx = 0;

  next();

  function next() {
    if (idx >= values.length) return;
    console.log(prefix + ": " + values[idx++]);
    next();
  }
}

handlerA(["x", "y"]);
handlerB(["1", "2"]);

// OUTPUT: A: x
// OUTPUT: A: y
// OUTPUT: B: 1
// OUTPUT: B: 2
