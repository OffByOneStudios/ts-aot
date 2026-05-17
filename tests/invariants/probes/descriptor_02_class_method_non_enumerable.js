// ECMA-262 §15.7 MethodDefinition: methods are installed on the prototype
// with {writable:true, enumerable:false, configurable:true}.

class C { m() { return 1; } }
var d = Object.getOwnPropertyDescriptor(C.prototype, "m");

if (d && d.enumerable === false && d.writable === true && d.configurable === true) {
  console.log("PASS");
} else {
  var got = d ? ("enum=" + d.enumerable + " writ=" + d.writable + " conf=" + d.configurable) : "no descriptor";
  console.log("FAIL: class method descriptor wrong: " + got);
}
