// ECMA-262 §10.4.2.4: Array exotic object's "length" property is
// {writable:true, enumerable:false, configurable:false}.

var a = [1, 2, 3];
var d = Object.getOwnPropertyDescriptor(a, "length");

if (d && d.value === 3 && d.writable === true && d.enumerable === false && d.configurable === false) {
  console.log("PASS");
} else {
  var got = d ? ("val=" + d.value + " writ=" + d.writable + " enum=" + d.enumerable + " conf=" + d.configurable) : "no descriptor";
  console.log("FAIL: Array.length descriptor wrong: " + got);
}
