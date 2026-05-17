// ECMA-262 §10.1.10 OrdinaryDelete: deleting a configurable own data
// property removes it; hasOwnProperty afterwards is false.

class C { x = 1; }
var c = new C();
var ok = delete c.x;

if (ok === true && !Object.prototype.hasOwnProperty.call(c, "x")) {
  console.log("PASS");
} else {
  console.log("FAIL: delete returned " + ok + ", hasOwn after = "
    + Object.prototype.hasOwnProperty.call(c, "x"));
}
