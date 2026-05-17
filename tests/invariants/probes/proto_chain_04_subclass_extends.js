// Subclass extends: Object.getPrototypeOf(Derived.prototype) === Base.prototype.

class Base {}
class Derived extends Base {}

if (Object.getPrototypeOf(Derived.prototype) === Base.prototype) {
  console.log("PASS");
} else {
  console.log("FAIL: Derived.prototype's [[Prototype]] !== Base.prototype");
}
