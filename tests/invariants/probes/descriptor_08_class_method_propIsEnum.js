// Methods on the prototype are non-enumerable. propertyIsEnumerable
// must reflect this — verifies the runtime is consistent with the
// descriptor.

class C { m() {} }
var c = new C();

if (C.prototype.propertyIsEnumerable("m") === false && c.propertyIsEnumerable("m") === false) {
  console.log("PASS");
} else {
  console.log("FAIL: class method 'm' should NOT be enumerable on prototype or instance");
}
