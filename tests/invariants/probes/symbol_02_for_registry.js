// Symbol.for(k) returns the SAME symbol on every call with the same key
// (global symbol registry).

var a = Symbol.for("shared");
var b = Symbol.for("shared");

if (a === b && Symbol.keyFor(a) === "shared") {
  console.log("PASS");
} else {
  console.log("FAIL: Symbol.for('shared') registry not honoring identity");
}
