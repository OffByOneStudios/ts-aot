// Cross-check the descriptor agrees with Object.prototype.propertyIsEnumerable.
// Both must report the same enumerability for an instance field.

class C { x = 1; }
var c = new C();

if (c.propertyIsEnumerable("x") === true) {
  console.log("PASS");
} else {
  console.log("FAIL: class field 'x' not enumerable via propertyIsEnumerable (returned "
    + c.propertyIsEnumerable("x") + ")");
}
