// Static methods live on the constructor function itself, not on
// instances. C.staticM must be the same value across reads.

class C { static staticM() { return 1; } }

if (C.staticM === C.staticM) {
  console.log("PASS");
} else {
  console.log("FAIL: C.staticM identity not stable");
}
