// Each call to Symbol() returns a unique symbol value.

var a = Symbol("x");
var b = Symbol("x");

if (a !== b && typeof a === "symbol" && typeof b === "symbol") {
  console.log("PASS");
} else {
  console.log("FAIL: Symbol('x') !== Symbol('x') invariant broken");
}
