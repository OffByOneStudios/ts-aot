// ECMA-262 §10.1.5 OrdinaryGet: a property accessed via the prototype
// chain returns the same value as a direct read of the prototype's slot.
// In particular, `c.m === C.prototype.m` for an instance method.

class C { m() { return 1; } }
var c = new C();

if (c.m === C.prototype.m) {
  console.log("PASS");
} else {
  console.log("FAIL: c.m !== C.prototype.m (instance method identity broken via prototype chain)");
}
