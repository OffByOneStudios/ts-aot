// ECMA-262 §15.7 ClassFieldDefinition: instance fields are defined via
// DefineOwnProperty with {writable:true, enumerable:true, configurable:true}.

class C { x = 42; }
var c = new C();
var d = Object.getOwnPropertyDescriptor(c, "x");

if (d && d.enumerable === true && d.writable === true && d.configurable === true) {
  console.log("PASS");
} else {
  var got = d ? ("enum=" + d.enumerable + " writ=" + d.writable + " conf=" + d.configurable) : "no descriptor";
  console.log("FAIL: class field descriptor wrong: " + got);
}
