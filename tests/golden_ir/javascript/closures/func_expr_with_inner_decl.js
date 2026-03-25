// Test: function expression with mutually recursive inner function declarations
// Exact Express pattern: proto.method = function(...) { function next() { handle(); } function handle() { next(); } }

var obj = {};

obj.run = function run(items, done) {
  var idx = 0;

  next();

  function next(err) {
    if (err) {
      done(err);
      return;
    }
    if (idx >= items.length) {
      done(null);
      return;
    }
    var item = items[idx++];
    handle(item);
  }

  function handle(item) {
    console.log("handle: " + item);
    next(null);
  }
};

obj.run(["one", "two", "three"], function(err) {
  console.log("done: err=" + err);
});

// OUTPUT: handle: one
// OUTPUT: handle: two
// OUTPUT: handle: three
// OUTPUT: done: err=null
