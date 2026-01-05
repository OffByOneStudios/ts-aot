// Bisect test 2: Add regex patterns that lodash uses
;(function() {
  var undefined;
  var VERSION = '4.17.21';

  /** Used to match property names within property paths. */
  var reIsDeepProp = /\.|\[(?:[^[\]]*|(["'])(?:(?!\1)[^\\]|\\.)*?\1)\]/,
      reIsPlainProp = /^\w*$/,
      rePropName = /[^.[\]]+|\[(?:(-?\d+(?:\.\d+)?)|(["'])((?:(?!\2)[^\\]|\\.)*?)\2)\]|(?=(?:\.|\[\])(?:\.|\[\]|$))/g;

  /** Used to match `RegExp` syntax characters. */
  var reRegExpChar = /[\\^$.*+?()[\]{}|]/g,
      reHasRegExpChar = RegExp(reRegExpChar.source);

  console.log("reIsDeepProp:", reIsDeepProp);
  console.log("reHasRegExpChar:", reHasRegExpChar);
  console.log("PASS: lodash_bisect_2");
}).call(this);
