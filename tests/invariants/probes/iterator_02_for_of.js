// for-of iterates element values in order.

var s = "";
for (var x of [1, 2, 3]) { s += x; }

if (s === "123") {
  console.log("PASS");
} else {
  console.log("FAIL: for-of order = " + JSON.stringify(s) + " (expected '123')");
}
