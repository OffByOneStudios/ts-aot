// Bisect test 7: Regex test patterns like lodash uses
;(function() {
  var undefined;

  /** Used to detect strings with [zero-width joiners or code points from the astral planes](http://eev.ee/blog/2015/09/12/dark-corners-of-unicode/). */
  var reHasUnicode = RegExp('[' +
    '\\u200d' +
    '\\ud800-\\udfff' +
    '\\u0300-\\u036f' +
    '\\ufe20-\\ufe2f' +
    '\\u20d0-\\u20ff' +
    '\\ufe0e\\ufe0f' +
    ']');

  /** Used to match a single whitespace character. */
  var reWhitespace = /\s/;

  /** Used to detect strings that need a more robust regexp to match words. */
  var reHasUnicodeWord = /[a-z][A-Z]|[A-Z]{2}[a-z]|[0-9][a-zA-Z]|[a-zA-Z][0-9]|[^a-zA-Z0-9 ]/;

  function hasUnicode(string) {
    return reHasUnicode.test(string);
  }

  function hasUnicodeWord(string) {
    return reHasUnicodeWord.test(string);
  }

  // Test the functions
  var tests = [
    ["hello", "hello"],
    ["Hello World", "Hello World"],
    ["café", "café"],
    ["😀", "emoji"]
  ];

  for (var i = 0; i < tests.length; i++) {
    var test = tests[i];
    var hasU = hasUnicode(test[0]);
    var hasUW = hasUnicodeWord(test[0]);
    console.log(test[1] + ": hasUnicode=" + hasU + ", hasUnicodeWord=" + hasUW);
  }

  console.log("PASS: lodash_bisect_7");
}).call(this);
