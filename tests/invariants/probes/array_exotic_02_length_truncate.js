// Setting .length to N truncates the array (the spec calls this
// ArraySetLength). Indices >= N become inaccessible.

var a = [1, 2, 3, 4, 5];
a.length = 2;

if (a.length === 2 && a[0] === 1 && a[1] === 2 && a[2] === undefined) {
  console.log("PASS");
} else {
  console.log("FAIL: after length=2: length=" + a.length + " a[2]=" + a[2]);
}
