// Bisect test 3: Environment detection like lodash
;(function() {
  var undefined;
  var VERSION = '4.17.21';
  
  /** Detect free variable `global` from Node.js. */
  var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;

  /** Detect free variable `self`. */
  var freeSelf = typeof self == 'object' && self && self.Object === Object && self;

  /** Used as a reference to the global object. */
  var root = freeGlobal || freeSelf || Function('return this')();

  /** Detect free variable `exports`. */
  var freeExports = typeof exports == 'object' && exports && !exports.nodeType && exports;

  /** Detect free variable `module`. */
  var freeModule = freeExports && typeof module == 'object' && module && !module.nodeType && module;

  /** Detect the popular CommonJS extension `module.exports`. */
  var moduleExports = freeModule && freeModule.exports === freeExports;

  /** Detect free variable `process` from Node.js. */
  var freeProcess = moduleExports && freeGlobal.process;

  console.log("freeGlobal:", typeof freeGlobal);
  console.log("freeSelf:", typeof freeSelf);
  console.log("root:", typeof root);
  console.log("freeExports:", typeof freeExports);
  console.log("moduleExports:", moduleExports);
  console.log("PASS: lodash_bisect_3");
}).call(this);
