// Sparse arrays: creating an array with `length` larger than initialized
// elements leaves "holes". Holes are NOT own properties; hasOwnProperty
// returns false for them.

var a = [1, , 3];

if (a.length === 3
    && a[1] === undefined
    && Object.prototype.hasOwnProperty.call(a, 0) === true
    && Object.prototype.hasOwnProperty.call(a, 1) === false
    && Object.prototype.hasOwnProperty.call(a, 2) === true) {
  console.log("PASS");
} else {
  console.log("FAIL: sparse-array hole semantics broken");
}
