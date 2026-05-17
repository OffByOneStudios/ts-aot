// A subclass instance accessing an inherited method returns the same
// function as the base class's prototype slot.

class Base { greet() { return 1; } }
class Derived extends Base {}
var d = new Derived();

if (d.greet === Base.prototype.greet) {
  console.log("PASS");
} else {
  console.log("FAIL: inherited method identity broken (subclass.method !== base.prototype.method)");
}
