// Test: Simulates lodash's exact initialization pattern
// This is a stripped-down version of lodash's UMD wrapper

;(function() {
  /** Used as a reference to the global object. */
  var root = (typeof self == 'object' && self && self.Object === Object && self) ||
             (typeof global == 'object' && global && global.Object === Object && global) ||
             this;

  /** Used to detect overreaching core-js shims. */
  var coreJsData = root['__core-js_shared__'];

  /** Used to detect methods masquerading as native. */
  var maskSrcKey = (function() {
    var uid = 'uid';
    var keys = [];
    return uid;
  }());

  console.log("Phase 1: Setup complete");

  /** Built-in value references. */
  var Symbol = root.Symbol,
      objectProto = Object.prototype,
      hasOwnProperty = objectProto.hasOwnProperty,
      propertyIsEnumerable = objectProto.propertyIsEnumerable;

  console.log("Phase 2: Built-ins referenced");

  /**
   * Creates a unary function that invokes `func` with its argument transformed.
   */
  function overArg(func, transform) {
    return function(arg) {
      return func(transform(arg));
    };
  }

  console.log("Phase 3: Helper functions defined");

  // Simulate lodash type checks
  var funcProto = Function.prototype,
      objectCtorString = funcProto.toString.call(Object);

  /** Used to lookup unminified function names. */
  var realNames = {};

  console.log("Phase 4: Type checks setup");

  // Create the main lodash object
  function lodash(value) {
    return value;
  }

  console.log("Phase 5: lodash function created");

  // Add methods to lodash
  lodash.VERSION = '4.17.21';
  
  lodash.identity = function(value) {
    return value;
  };

  lodash.noop = function() {};

  console.log("Phase 6: Methods added");

  // Export
  root._ = lodash;
  
  console.log("PASS: lodash_init_sim");
}).call(this);
