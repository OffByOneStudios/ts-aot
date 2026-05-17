// C.prototype.constructor === C, and (new C()).constructor === C.

class C {}
var c = new C();

if (C.prototype.constructor === C && c.constructor === C) {
  console.log("PASS");
} else {
  console.log("FAIL: C.prototype.constructor or c.constructor not equal to C");
}
