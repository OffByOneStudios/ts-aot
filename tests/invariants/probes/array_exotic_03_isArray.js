// Array.isArray distinguishes Arrays from objects. Even array-like
// objects (e.g. {length:0}) are not Arrays.

if (Array.isArray([]) === true
    && Array.isArray([1, 2, 3]) === true
    && Array.isArray({length: 0}) === false
    && Array.isArray("abc") === false
    && Array.isArray(null) === false) {
  console.log("PASS");
} else {
  console.log("FAIL: Array.isArray returned wrong value for one of: [], [1,2,3], {length:0}, 'abc', null");
}
