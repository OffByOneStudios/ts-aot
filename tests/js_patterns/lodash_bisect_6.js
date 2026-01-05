// Bisect test 6: Type checking functions (the toString.call pattern)
;(function() {
  var undefined;

  /** Used for built-in method references. */
  var objectProto = Object.prototype;

  /** Used to check objects for own properties. */
  var hasOwnProperty = objectProto.hasOwnProperty;

  /**
   * Used to resolve the
   * [`toStringTag`](http://ecma-international.org/ecma-262/7.0/#sec-object.prototype.tostring)
   * of values.
   */
  var nativeObjectToString = objectProto.toString;

  /** Built-in value reference. */
  var symToStringTag = undefined; // Symbol.toStringTag not available

  /**
   * Converts `value` to a string using `Object.prototype.toString`.
   *
   * @private
   * @param {*} value The value to convert.
   * @returns {string} Returns the converted string.
   */
  function objectToString(value) {
    return nativeObjectToString.call(value);
  }

  /**
   * A specialized version of `baseGetTag` which ignores `Symbol.toStringTag` values.
   *
   * @private
   * @param {*} value The value to query.
   * @returns {string} Returns the raw `toStringTag`.
   */
  function getRawTag(value) {
    var isOwn = hasOwnProperty.call(value, symToStringTag),
        tag = value[symToStringTag];

    return nativeObjectToString.call(value);
  }

  /**
   * The base implementation of `getTag` without fallbacks for buggy environments.
   *
   * @private
   * @param {*} value The value to query.
   * @returns {string} Returns the `toStringTag`.
   */
  function baseGetTag(value) {
    if (value == null) {
      return value === undefined ? '[object Undefined]' : '[object Null]';
    }
    if (symToStringTag && symToStringTag in Object(value)) {
      return getRawTag(value);
    }
    return objectToString(value);
  }

  // Test type checking
  var tests = [
    ["array", []],
    ["object", {}],
    ["function", function() {}],
    ["string", "hello"],
    ["number", 42],
    ["null", null],
    ["undefined", undefined]
  ];

  for (var i = 0; i < tests.length; i++) {
    var test = tests[i];
    var tag = baseGetTag(test[1]);
    console.log(test[0] + ": " + tag);
  }

  console.log("PASS: lodash_bisect_6");
}).call(this);
