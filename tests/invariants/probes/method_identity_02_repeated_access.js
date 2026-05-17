// ECMA-262 §10.1.5: repeated reads of the same property on the same
// receiver return the same value. A method access cached as a function
// must be reference-equal across reads.

class C { m() { return 1; } }
var c = new C();
var a = c.m;
var b = c.m;

if (a === b) {
  console.log("PASS");
} else {
  console.log("FAIL: c.m !== c.m on repeated access");
}
