// ECMA-262 §10.2.4 (Function.length): {writable:false, enumerable:false,
// configurable:true}. Reading it should produce that descriptor exactly.

function f(a, b, c) {}
var d = Object.getOwnPropertyDescriptor(f, "length");

if (d && d.value === 3 && d.writable === false && d.enumerable === false && d.configurable === true) {
  console.log("PASS");
} else {
  var got = d ? ("val=" + d.value + " writ=" + d.writable + " enum=" + d.enumerable + " conf=" + d.configurable) : "no descriptor";
  console.log("FAIL: Function.length descriptor wrong: " + got);
}
