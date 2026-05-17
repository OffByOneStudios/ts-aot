// Pushing to an array updates .length atomically.

var a = [1, 2, 3];
a.push(4);

if (a.length === 4 && a[3] === 4) {
  console.log("PASS");
} else {
  console.log("FAIL: after push, length=" + a.length + " a[3]=" + a[3]);
}
