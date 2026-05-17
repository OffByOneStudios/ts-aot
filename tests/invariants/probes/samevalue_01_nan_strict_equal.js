// NaN !== NaN per ECMA-262 §7.2.15 (Strict Equality Comparison).

if ((NaN === NaN) === false) {
  console.log("PASS");
} else {
  console.log("FAIL: NaN === NaN returned true (must be false per spec)");
}
