// Object.is(NaN, NaN) === true per ECMA-262 SameValue.

if (Object.is(NaN, NaN) === true) {
  console.log("PASS");
} else {
  console.log("FAIL: Object.is(NaN, NaN) returned false (must be true per spec)");
}
