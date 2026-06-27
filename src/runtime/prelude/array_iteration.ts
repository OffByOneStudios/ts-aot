// Self-hosted Array iteration builtins.
//
// Spec-correct implementations installed via globalThis.__defineBuiltin. The C++
// natives stay the FAST path (packed arrays, clean Array.prototype); these run
// only as the spec bailout (array-like receivers, or holey arrays whose holes
// could inherit indices from a modified Array.prototype). See
// src/runtime/src/TsBuiltinInstall.cpp and the per-native dispatch in
// TsArray.cpp / TsObject_Builtins.cpp.
//
// Each method takes the receiver as an explicit first parameter `O_recv` (not
// `this`): the prelude compiles as plain module-level function expressions where
// `this` isn't bound, and the native dispatch invokes SH(receiver, cb, thisArg).
//
// INTRINSIC SURFACE (perf): the hot per-element operations below — `k in O`,
// `O[k]`, `cb.call(...)`, and the result writes — are the operations that decide
// how fast self-hosted builtins run. Today they lower to the runtime's dynamic
// property ops (which already fast-path real arrays). To make hosted code
// competitive with the C++ natives when it becomes the primary path, these
// should become compiler-lowered intrinsics (`%LoadElement`/`%HasElement`/
// `%Call`/`%CreateDataProperty`, à la JSC @-intrinsics / V8 Torque). The shared
// abstract-op helpers here (toLength/toObject/isCallable) are the same surface at
// the coercion level. Keeping every method on this shared surface means that perf
// work lands once, in the intrinsics, not across N hand-written methods.

(function () {
  // --- Shared abstract operations (the coercion-level intrinsic surface) ---

  // ECMA-262 7.1.20 ToLength: ToInteger then clamp to [0, 2^53 - 1].
  function toLength(v: any): number {
    var n = Number(v);
    if (n !== n) return 0; // NaN
    n = (n < 0 ? -1 : 1) * Math.floor(Math.abs(n));
    if (n <= 0) return 0;
    return n > 9007199254740991 ? 9007199254740991 : n;
  }

  // ECMA-262 7.1.18 ToObject after 7.2.1 RequireObjectCoercible. Returns the
  // wrapper object, throwing TypeError for null/undefined.
  function toObject(o: any, method: string): any {
    if (o === null || o === undefined) {
      throw new TypeError("Array.prototype." + method + " called on null or undefined");
    }
    return Object(o);
  }

  function requireCallable(f: any): void {
    if (typeof f !== "function") {
      throw new TypeError("callbackfn is not a function");
    }
  }

  // --- 23.1.3.7 Array.prototype.filter (compacts; holes skipped) ---
  var filter = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "filter");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    var A: any[] = [];
    var to = 0;
    for (var k = 0; k < len; k++) {
      if (k in O) {
        var kValue = O[k];
        if (callbackfn.call(thisArg, kValue, k, O)) {
          A[to] = kValue;
          to++;
        }
      }
    }
    return A;
  };

  // --- 23.1.3.19 Array.prototype.map (length-preserving; holes preserved) ---
  var map = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "map");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    // length len, all holes; present indices are filled below, absent stay holes.
    var A: any[] = new Array(len);
    for (var k = 0; k < len; k++) {
      if (k in O) {
        A[k] = callbackfn.call(thisArg, O[k], k, O);
      }
    }
    return A;
  };

  // --- 23.1.3.15 Array.prototype.forEach ---
  var forEach = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "forEach");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    for (var k = 0; k < len; k++) {
      if (k in O) callbackfn.call(thisArg, O[k], k, O);
    }
    return undefined;
  };

  // --- 23.1.3.28 Array.prototype.some (∃) ---
  var some = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "some");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    for (var k = 0; k < len; k++) {
      if (k in O && callbackfn.call(thisArg, O[k], k, O)) return true;
    }
    return false;
  };

  // --- 23.1.3.6 Array.prototype.every (∀) ---
  var every = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "every");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    for (var k = 0; k < len; k++) {
      if (k in O && !callbackfn.call(thisArg, O[k], k, O)) return false;
    }
    return true;
  };

  var def = (globalThis as any).__defineBuiltin;
  def(Array.prototype, "filter", 1, filter);
  def(Array.prototype, "map", 1, map);
  def(Array.prototype, "forEach", 1, forEach);
  def(Array.prototype, "some", 1, some);
  def(Array.prototype, "every", 1, every);
})();
