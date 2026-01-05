// Bisect test 4: runInContext pattern with native functions
;(function() {
  var undefined;
  var VERSION = '4.17.21';
  
  var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
  var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
  var root = freeGlobal || freeSelf || Function('return this')();

  /** Built-in value references. */
  var Buffer = undefined;  // not available in ts-aot
  var Symbol = root.Symbol,
      Uint8Array = root.Uint8Array,
      propertyIsEnumerable = Object.prototype.propertyIsEnumerable,
      splice = Array.prototype.splice;

  /* Built-in method references for those with the same name as other `lodash` methods. */
  var nativeCeil = Math.ceil,
      nativeFloor = Math.floor,
      nativeGetSymbols = Object.getOwnPropertySymbols,
      nativeIsBuffer = Buffer ? Buffer.isBuffer : undefined,
      nativeIsFinite = root.isFinite,
      nativeJoin = Array.prototype.join,
      nativeKeys = Object.keys,
      nativeMax = Math.max,
      nativeMin = Math.min,
      nativeNow = Date.now,
      nativeParseInt = root.parseInt,
      nativeRandom = Math.random,
      nativeReverse = Array.prototype.reverse;

  console.log("nativeCeil:", typeof nativeCeil);
  console.log("nativeFloor:", typeof nativeFloor);
  console.log("nativeKeys:", typeof nativeKeys);
  console.log("nativeMax:", typeof nativeMax);
  console.log("nativeNow:", typeof nativeNow);
  console.log("PASS: lodash_bisect_4");
}).call(this);
