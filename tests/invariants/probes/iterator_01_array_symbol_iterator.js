// Arrays have a [Symbol.iterator] method (the spec @@iterator slot).
// Calling it returns an iterator with a .next() method.

var a = [1, 2, 3];
var it = a[Symbol.iterator]();

if (typeof it.next === "function") {
  var first = it.next();
  if (first.value === 1 && first.done === false) {
    console.log("PASS");
  } else {
    console.log("FAIL: first .next() returned {value: " + first.value + ", done: " + first.done + "}");
  }
} else {
  console.log("FAIL: array's Symbol.iterator did not yield iterator with .next()");
}
