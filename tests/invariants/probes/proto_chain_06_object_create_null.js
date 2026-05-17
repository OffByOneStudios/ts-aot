// Object.create(null) produces an object with no prototype.

var o = Object.create(null);
if (Object.getPrototypeOf(o) === null) {
  console.log("PASS");
} else {
  console.log("FAIL: Object.create(null) has non-null prototype");
}
