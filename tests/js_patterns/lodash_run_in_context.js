// Simulating the key runInContext pattern
;(function() {
  var undefined;
  var VERSION = '4.17.21';

  var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
  var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
  var root = freeGlobal || freeSelf || Function('return this')();

  /**
   * A faster alternative to `Function#apply`, this function invokes `func`
   * with the `this` binding of `thisArg` and the arguments of `args`.
   *
   * @private
   * @param {Function} func The function to invoke.
   * @param {*} thisArg The `this` binding of `func`.
   * @param {Array} args The arguments to invoke `func` with.
   * @returns {*} Returns the result of `func`.
   */
  function apply(func, thisArg, args) {
    switch (args.length) {
      case 0: return func.call(thisArg);
      case 1: return func.call(thisArg, args[0]);
      case 2: return func.call(thisArg, args[0], args[1]);
      case 3: return func.call(thisArg, args[0], args[1], args[2]);
    }
    return func.apply(thisArg, args);
  }

  /**
   * Creates a lodash object which wraps `value` to enable implicit method chain sequences.
   *
   * @static
   * @memberOf _
   * @category Seq
   * @param {*} value The value to wrap.
   * @returns {Object} Returns the new `lodash` wrapper instance.
   */
  var runInContext = (function runInContext(context) {
    // Get built-in constructor references
    var Array = root.Array,
        Date = root.Date,
        Error = root.Error,
        Function = root.Function,
        Math = root.Math,
        Object = root.Object,
        RegExp = root.RegExp,
        String = root.String,
        TypeError = root.TypeError;

    /** Used for built-in method references. */
    var arrayProto = Array.prototype,
        funcProto = Function.prototype,
        objectProto = Object.prototype;

    /** Used to detect overreaching core-js shims. */
    var coreJsData = root['__core-js_shared__'];

    /** Used to access faster Node.js helpers. */
    var freeProcess = undefined; // not available

    /** Used to check objects for own properties. */
    var hasOwnProperty = objectProto.hasOwnProperty;

    /** Used to generate unique IDs. */
    var idCounter = 0;

    console.log("Inside runInContext");
    console.log("Array:", typeof Array);
    console.log("Object:", typeof Object);
    console.log("arrayProto:", typeof arrayProto);
    console.log("objectProto:", typeof objectProto);
    console.log("hasOwnProperty:", typeof hasOwnProperty);

    /*------------------------------------------------------------------------*/

    /**
     * Creates a `lodash` object which wraps `value` to enable implicit method chain sequences.
     */
    function lodash(value) {
      return value;
    }

    /*------------------------------------------------------------------------*/

    // Add methods to `lodash`
    lodash.VERSION = VERSION;

    console.log("lodash created");

    return lodash;
  });

  // Create the lodash instance
  var _ = runInContext();

  console.log("_ type:", typeof _);
  console.log("_.VERSION:", _.VERSION);

  // Export
  root._ = _;

  console.log("PASS: lodash_run_in_context");
}).call(this);
