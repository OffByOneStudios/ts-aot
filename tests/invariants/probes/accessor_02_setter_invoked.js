// Setter is invoked on assignment with the RHS as argument.

var stored = null;
var o = { set v(x) { stored = x; } };
o.v = 42;

if (stored === 42) {
  console.log("PASS");
} else {
  console.log("FAIL: setter did not capture RHS (stored=" + stored + ")");
}
