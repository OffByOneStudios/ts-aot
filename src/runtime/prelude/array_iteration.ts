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

  // ECMA-262 23.1.3.3 (9.4.2.3) ArraySpeciesCreate(originalArray, length).
  // IsArray tunnels Proxy targets (7.2.2 step 3) via Array.isArray, so a
  // Proxy wrapping a real array with a custom constructor[@@species] builds
  // its result through that species (map/filter create-proxy family). A
  // non-array receiver takes ArrayCreate (step 4). Default-constructor
  // receivers return a plain array — identical to the previous behavior.
  function arraySpeciesCreate(O: any, len: number): any {
    if (!Array.isArray(O)) return new Array(len);
    var C: any = O.constructor;
    if (C !== null && C !== undefined &&
        (typeof C === "object" || typeof C === "function")) {
      C = C[Symbol.species];
      if (C === null) C = undefined;
    }
    if (C === undefined) return new Array(len);
    if (typeof C !== "function") {
      throw new TypeError("ArraySpeciesCreate: @@species is not a constructor");
    }
    return new C(len);
  }

  // --- 23.1.3.7 Array.prototype.filter (compacts; holes skipped) ---
  var filter = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "filter");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    var A: any = arraySpeciesCreate(O, 0);  // 23.1.3.8 step 4
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
    var A: any = arraySpeciesCreate(O, len);  // 23.1.3.19 step 5
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

  // --- 23.1.3.24 Array.prototype.reduce. The native dispatch passes the explicit
  // `hasInitial` flag (initialValue == undefined is ambiguous; the spec needs the
  // "was an argument supplied" bit to pick the seed and the empty-array throw). ---
  var reduce = function (O_recv: any, callbackfn: any, initialValue: any, hasInitial: any): any {
    var O: any = toObject(O_recv, "reduce");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    var k = 0;
    var acc: any;
    if (hasInitial) {
      acc = initialValue;
    } else {
      var found = false;
      while (k < len && !found) {
        if (k in O) { acc = O[k]; found = true; }
        k++;
      }
      if (!found) throw new TypeError("Reduce of empty array with no initial value");
    }
    while (k < len) {
      if (k in O) acc = callbackfn.call(undefined, acc, O[k], k, O);
      k++;
    }
    return acc;
  };

  // --- 23.1.3.25 Array.prototype.reduceRight (right-to-left) ---
  var reduceRight = function (O_recv: any, callbackfn: any, initialValue: any, hasInitial: any): any {
    var O: any = toObject(O_recv, "reduceRight");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    var k = len - 1;
    var acc: any;
    if (hasInitial) {
      acc = initialValue;
    } else {
      var found = false;
      while (k >= 0 && !found) {
        if (k in O) { acc = O[k]; found = true; }
        k--;
      }
      if (!found) throw new TypeError("Reduce of empty array with no initial value");
    }
    while (k >= 0) {
      if (k in O) acc = callbackfn.call(undefined, acc, O[k], k, O);
      k--;
    }
    return acc;
  };

  // --- 23.1.3.9 Array.prototype.find / 23.1.3.10 findIndex (visit ALL indices;
  // holes are observed as undefined via Get, not skipped) ---
  var find = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "find");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    for (var k = 0; k < len; k++) {
      var v = O[k];
      if (callbackfn.call(thisArg, v, k, O)) return v;
    }
    return undefined;
  };

  var findIndex = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "findIndex");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    for (var k = 0; k < len; k++) {
      if (callbackfn.call(thisArg, O[k], k, O)) return k;
    }
    return -1;
  };

  // --- 23.1.3.11 Array.prototype.flatMap (map then flatten one level) ---
  var flatMap = function (O_recv: any, callbackfn: any, thisArg: any): any {
    var O: any = toObject(O_recv, "flatMap");
    var len: number = toLength(O.length);
    requireCallable(callbackfn);
    var A: any = arraySpeciesCreate(O, 0);  // 23.1.3.13 step 5
    var to = 0;
    for (var k = 0; k < len; k++) {
      if (k in O) {
        var v = callbackfn.call(thisArg, O[k], k, O);
        if (Array.isArray(v)) {
          var vlen = toLength(v.length);
          for (var j = 0; j < vlen; j++) {
            if (j in v) { A[to] = v[j]; to++; }
          }
        } else {
          A[to] = v; to++;
        }
      }
    }
    return A;
  };

  var def = (globalThis as any).__defineBuiltin;
  def(Array.prototype, "filter", 1, filter);
  def(Array.prototype, "map", 1, map);
  def(Array.prototype, "forEach", 1, forEach);
  def(Array.prototype, "some", 1, some);
  def(Array.prototype, "every", 1, every);
  def(Array.prototype, "reduce", 1, reduce);
  def(Array.prototype, "reduceRight", 1, reduceRight);
  def(Array.prototype, "find", 1, find);
  def(Array.prototype, "findIndex", 1, findIndex);
  def(Array.prototype, "flatMap", 1, flatMap);
})();
