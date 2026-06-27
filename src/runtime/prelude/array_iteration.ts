// Self-hosted Array iteration builtins.
//
// Spec-correct implementations installed via globalThis.__defineBuiltin. The C++
// natives stay the fast path (packed arrays, clean Array.prototype); these run
// only as the spec bailout (array-like receivers, holey arrays with inherited
// indices). See src/runtime/src/TsBuiltinInstall.cpp and the per-native dispatch
// in TsArray.cpp / TsObject_Builtins.cpp.
//
// Each file under src/runtime/prelude/ is concatenated (sorted) into one prelude
// module that the Driver prepends ahead of user code. Wrap in an IIFE so locals
// don't collide across prelude files.

(function () {
  // ECMA-262 ToLength: ToInteger then clamp to [0, 2^53 - 1].
  function toLength(v: any): number {
    var n = Number(v);
    if (n !== n) return 0; // NaN
    n = (n < 0 ? -1 : 1) * Math.floor(Math.abs(n));
    if (n <= 0) return 0;
    return n > 9007199254740991 ? 9007199254740991 : n;
  }

  // 23.1.3.7 Array.prototype.filter. The receiver is passed as the explicit
  // first parameter `receiver` (not `this`): this prelude compiles as a plain
  // module function expression, where `this` isn't bound, and the native
  // dispatch (TsArray.cpp / TsObject_Builtins.cpp) invokes it as
  // SH(receiver, callbackfn, thisArg).
  var filter = function (receiver: any, callbackfn: any, thisArg: any): any {
    if (receiver === null || receiver === undefined) {
      throw new TypeError("Array.prototype.filter called on null or undefined");
    }
    var O: any = Object(receiver);
    var len: number = toLength(O.length);
    if (typeof callbackfn !== "function") {
      throw new TypeError("callbackfn is not a function");
    }
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

  (globalThis as any).__defineBuiltin(Array.prototype, "filter", 1, filter);
})();
