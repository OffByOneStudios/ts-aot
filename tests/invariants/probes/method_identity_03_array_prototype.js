// Built-in prototype methods are shared. [1,2,3].map and [4,5].map must be
// the same function value, equal to Array.prototype.map.

var a = [1, 2, 3];
var b = [4, 5];

if (a.map === b.map && a.map === Array.prototype.map) {
  console.log("PASS");
} else {
  console.log("FAIL: Array.prototype.map identity not shared across instances");
}
