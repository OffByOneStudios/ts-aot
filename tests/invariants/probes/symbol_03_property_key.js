// Symbols can be used as property keys. Read-back must return what was set.

var s = Symbol("k");
var o = {};
o[s] = "value";

if (o[s] === "value") {
  console.log("PASS");
} else {
  console.log("FAIL: symbol-keyed property read returned " + o[s]);
}
