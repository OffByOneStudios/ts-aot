// Test: inner function declarations used as middleware callbacks with recursion
// The core Express/Connect middleware chain pattern.

function runMiddleware(stack, req) {
  var idx = 0;

  next();

  function next() {
    if (idx >= stack.length) {
      console.log("chain complete for " + req);
      return;
    }
    var fn = stack[idx++];
    fn(req, next);
  }
}

var mw1 = function(req, next) { console.log("mw1: " + req); next(); };
var mw2 = function(req, next) { console.log("mw2: " + req); next(); };
var mw3 = function(req, next) { console.log("mw3: " + req); next(); };

runMiddleware([mw1, mw2, mw3], "/home");

// OUTPUT: mw1: /home
// OUTPUT: mw2: /home
// OUTPUT: mw3: /home
// OUTPUT: chain complete for /home
