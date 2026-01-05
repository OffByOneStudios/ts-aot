// Bisect test 5: runInContext function definition pattern
;(function() {
  var undefined;
  var VERSION = '4.17.21';
  
  var freeGlobal = typeof global == 'object' && global && global.Object === Object && global;
  var freeSelf = typeof self == 'object' && self && self.Object === Object && self;
  var root = freeGlobal || freeSelf || Function('return this')();

  var nativeCeil = Math.ceil;
  var nativeFloor = Math.floor;
  var nativeMax = Math.max;
  var nativeMin = Math.min;

  /**
   * The function whose prototype chain sequence wrappers inherit from.
   *
   * @private
   */
  function baseLodash() {
    // No operation performed.
  }

  /**
   * The base implementation of `_.create` without support for assigning
   * properties to the created object.
   *
   * @private
   * @param {Object} proto The object to inherit from.
   * @returns {Object} Returns the new object.
   */
  function baseCreate(proto) {
    return typeof proto === 'object' ? Object.create(proto) : {};
  }

  /**
   * The base implementation of `_.property` without support for deep paths.
   *
   * @private
   * @param {string} key The key of the property to get.
   * @returns {Function} Returns the new accessor function.
   */
  function baseProperty(key) {
    return function(object) {
      return object == null ? undefined : object[key];
    };
  }

  /**
   * The base implementation of `_.clamp` which doesn't coerce arguments.
   *
   * @private
   * @param {number} number The number to clamp.
   * @param {number} [lower] The lower bound.
   * @param {number} upper The upper bound.
   * @returns {number} Returns the clamped number.
   */
  function baseClamp(number, lower, upper) {
    if (number === number) {
      if (upper !== undefined) {
        number = number <= upper ? number : upper;
      }
      if (lower !== undefined) {
        number = number >= lower ? number : lower;
      }
    }
    return number;
  }

  // Test the functions
  var proto = { foo: 'bar' };
  var obj = baseCreate(proto);
  console.log("baseCreate works:", typeof obj === 'object');

  var getX = baseProperty('x');
  var testObj = { x: 42 };
  console.log("baseProperty works:", getX(testObj) === 42);

  var clamped = baseClamp(15, 0, 10);
  console.log("baseClamp works:", clamped === 10);

  console.log("PASS: lodash_bisect_5");
}).call(this);
