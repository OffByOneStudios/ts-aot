// ECMA-262 §10.2.3 (Function.name): {writable:false, enumerable:false,
// configurable:true}.

function fooBar() {}
var d = Object.getOwnPropertyDescriptor(fooBar, "name");

if (d && d.value === "fooBar" && d.writable === false && d.enumerable === false && d.configurable === true) {
  console.log("PASS");
} else {
  var got = d ? ("val=" + JSON.stringify(d.value) + " writ=" + d.writable + " enum=" + d.enumerable + " conf=" + d.configurable) : "no descriptor";
  console.log("FAIL: Function.name descriptor wrong: " + got);
}
