// Getter is invoked on every read; returns the getter's return value.

var n = 0;
var o = { get v() { n++; return 42; } };
var x = o.v;
var y = o.v;

if (x === 42 && y === 42 && n === 2) {
  console.log("PASS");
} else {
  console.log("FAIL: x=" + x + " y=" + y + " n=" + n + " (expected 42, 42, 2)");
}
