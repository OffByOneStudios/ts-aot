// A subclass instance is an instanceof both subclass and superclass.

class Base {}
class Derived extends Base {}
var d = new Derived();

if (d instanceof Derived && d instanceof Base) {
  console.log("PASS");
} else {
  console.log("FAIL: (new Derived()) instanceof Derived=" + (d instanceof Derived)
    + " Base=" + (d instanceof Base));
}
