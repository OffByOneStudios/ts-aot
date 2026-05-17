// ECMA-262 §10.1.1: Object.getPrototypeOf(new C()) === C.prototype.

class C {}
var c = new C();

if (Object.getPrototypeOf(c) === C.prototype) {
  console.log("PASS");
} else {
  console.log("FAIL: Object.getPrototypeOf(new C()) !== C.prototype");
}
