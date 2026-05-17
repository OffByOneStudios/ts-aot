// Function .length = number of params before any with a default or rest.

function a() {}
function b(x, y) {}
function c(x, y = 1) {}      // .length = 1 (stops at default)
function d(x, ...rest) {}    // .length = 1 (stops at rest)

if (a.length === 0 && b.length === 2 && c.length === 1 && d.length === 1) {
  console.log("PASS");
} else {
  console.log("FAIL: function .length wrong: a=" + a.length + " b=" + b.length
    + " c=" + c.length + " d=" + d.length);
}
