// Deleted properties must NOT appear in Object.keys.

class C { x = 1; y = 2; }
var c = new C();
delete c.x;
var k = Object.keys(c).sort().join(",");

if (k === "y") {
  console.log("PASS");
} else {
  console.log("FAIL: Object.keys after delete = " + JSON.stringify(k) + " (expected 'y')");
}
