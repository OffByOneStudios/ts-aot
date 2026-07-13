// Builtin prototype-surface contract (SMELL-002 follow-up, 2026-07-12).
//
// WHY THIS EXISTS: several builtins implement their methods in per-instance
// dispatch (GetPropertyVirtual / magic-tagged arms in ts_object_get_property)
// while the `.prototype` object exposed on the constructor is a separate map
// that only carries what is explicitly installed on it. Those two surfaces
// drift: a method added to the instance dispatch but never mirrored onto the
// prototype works on `instance.m()` yet reads undefined via
// `Ctor.prototype.m` — breaking .call/.apply extraction, feature detection,
// and every test262 verifyProperty check (ArrayBuffer.prototype.slice/
// transfer/transferToFixedLength shipped that way).
//
// CONTRACT: for every method named below, BOTH surfaces must agree:
//   typeof Ctor.prototype[name] === "function"
//   typeof instance[name]       === "function"
// When adding a builtin method, add its name here. If this test fails after
// your change, you added the method to only one surface — install it on the
// prototype (preferred: instance reads then inherit) or wire both.

function user_main() {
  var failures = 0;

  function checkSurfaces(label, ctor, instance, methods) {
    if (typeof ctor === "undefined") {
      console.log('FAIL: ' + label + ' constructor missing');
      failures = failures + 1;
      return;
    }
    var proto = ctor.prototype;
    if (!proto) {
      console.log('FAIL: ' + label + '.prototype missing');
      failures = failures + 1;
      return;
    }
    for (var i = 0; i < methods.length; i++) {
      var m = methods[i];
      var onProto = typeof proto[m] === "function";
      var onInst = typeof instance[m] === "function";
      if (onProto && onInst) {
        console.log('PASS: ' + label + '.' + m);
      } else {
        console.log('FAIL: ' + label + '.' + m + ' proto=' + onProto + ' instance=' + onInst);
        failures = failures + 1;
      }
    }
  }

  checkSurfaces("ArrayBuffer", ArrayBuffer, new ArrayBuffer(8),
    ["slice", "resize", "transfer", "transferToFixedLength"]);

  checkSurfaces("Array", Array, [],
    ["push", "pop", "slice", "map", "filter", "forEach", "indexOf", "join",
     "concat", "reduce", "includes", "find", "flat", "at", "keys", "values",
     "entries", "fill", "copyWithin", "sort", "reverse", "some", "every"]);

  checkSurfaces("String", String, "x",
    ["charAt", "slice", "indexOf", "split", "replace", "trim", "includes",
     "startsWith", "endsWith", "padStart", "padEnd", "repeat", "at",
     "toLowerCase", "toUpperCase", "codePointAt", "normalize", "matchAll"]);

  checkSurfaces("RegExp", RegExp, /a/,
    ["test", "exec", "toString", "compile"]);

  checkSurfaces("Date", Date, new Date(0),
    ["getTime", "getFullYear", "toISOString", "toJSON", "valueOf",
     "setFullYear", "getUTCHours", "toDateString"]);

  checkSurfaces("Map", Map, new Map(),
    ["get", "set", "has", "delete", "clear", "forEach", "keys", "values", "entries"]);

  checkSurfaces("Set", Set, new Set(),
    ["add", "has", "delete", "clear", "forEach", "keys", "values", "entries",
     "union", "intersection", "difference"]);

  checkSurfaces("Promise", Promise, Promise.resolve(1),
    ["then", "catch", "finally"]);

  checkSurfaces("Function", Function, function f() {},
    ["call", "apply", "bind", "toString"]);

  console.log('\n=== Summary ===');
  if (failures === 0) {
    console.log('All tests passed!');
  } else {
    console.log(failures + ' test(s) failed');
  }
  return failures;
}
