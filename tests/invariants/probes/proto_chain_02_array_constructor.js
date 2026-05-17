// [].constructor === Array. The constructor property climbs through
// Array.prototype.constructor.

var a = [];
if (a.constructor === Array) {
  console.log("PASS");
} else {
  console.log("FAIL: [].constructor !== Array");
}
