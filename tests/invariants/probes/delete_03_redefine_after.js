// After delete, reassignment restores the property as a new configurable
// data property (writes don't fail and the value reads back).

class C { x = 1; }
var c = new C();
delete c.x;
c.x = 42;

if (c.x === 42 && Object.prototype.hasOwnProperty.call(c, "x")) {
  console.log("PASS");
} else {
  console.log("FAIL: re-assigned x reads back as " + c.x
    + " hasOwn=" + Object.prototype.hasOwnProperty.call(c, "x"));
}
