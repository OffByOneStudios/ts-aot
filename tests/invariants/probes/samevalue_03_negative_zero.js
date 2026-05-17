// +0 === -0 is true but Object.is(+0, -0) is false. SameValue
// distinguishes signed zero; strict equality does not.

if ((0 === -0) === true && Object.is(0, -0) === false) {
  console.log("PASS");
} else {
  console.log("FAIL: 0===-0 was " + (0===-0) + ", Object.is(0,-0) was " + Object.is(0,-0));
}
