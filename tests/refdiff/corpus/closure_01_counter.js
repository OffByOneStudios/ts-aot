function makeCounter() {
  var n = 0;
  return {
    inc: function() { return ++n; },
    get: function() { return n; },
    reset: function() { n = 0; }
  };
}

var c = makeCounter();
console.log(c.inc());
console.log(c.inc());
console.log(c.inc());
console.log("get:", c.get());
c.reset();
console.log("after reset:", c.get(), "then inc:", c.inc());
