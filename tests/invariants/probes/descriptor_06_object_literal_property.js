// ECMA-262 §13.2.5 PropertyDefinition: object literal data properties
// have {writable:true, enumerable:true, configurable:true}.

var o = { foo: 1 };
var d = Object.getOwnPropertyDescriptor(o, "foo");

if (d && d.value === 1 && d.writable === true && d.enumerable === true && d.configurable === true) {
  console.log("PASS");
} else {
  var got = d ? ("val=" + d.value + " writ=" + d.writable + " enum=" + d.enumerable + " conf=" + d.configurable) : "no descriptor";
  console.log("FAIL: object-literal property descriptor wrong: " + got);
}
