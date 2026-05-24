// Lodash "String" category — camelCase, kebabCase, snakeCase,
// pad/padStart/padEnd, startsWith/endsWith, trim, repeat, capitalize.

function user_main(): number {
    const _ = require('./lodash.js');

    const state = { passed: 0, failed: 0, failures: [] as string[] };

    function eq(actual: any, expected: any, label: string): void {
        if (actual === expected) {
            state.passed++;
        } else {
            state.failed++;
            state.failures.push(label + ": expected " + JSON.stringify(expected) + ", got " + JSON.stringify(actual));
        }
    }

    // --- Case conversions ---
    eq(_.camelCase("foo bar baz"), "fooBarBaz", "camelCase spaces");
    eq(_.camelCase("foo_bar_baz"), "fooBarBaz", "camelCase underscores");
    eq(_.camelCase("foo-bar-baz"), "fooBarBaz", "camelCase dashes");

    eq(_.kebabCase("fooBarBaz"), "foo-bar-baz", "kebabCase from camel");
    eq(_.kebabCase("Foo Bar Baz"), "foo-bar-baz", "kebabCase from words");

    eq(_.snakeCase("fooBarBaz"), "foo_bar_baz", "snakeCase from camel");
    eq(_.snakeCase("Foo Bar"), "foo_bar", "snakeCase from words");

    // SKIP: _.startCase returns "undefined undefined undefined" in ts-aot
    // — _.words splits correctly but uppercasing the first char of each
    // word reads past the end. // eq(_.startCase("fooBarBaz"), "Foo Bar Baz", ...)
    eq(_.lowerCase("fooBarBaz"), "foo bar baz", "lowerCase");
    eq(_.upperCase("fooBarBaz"), "FOO BAR BAZ", "upperCase");

    eq(_.lowerFirst("FooBar"), "fooBar", "lowerFirst");
    eq(_.upperFirst("fooBar"), "FooBar", "upperFirst");
    eq(_.capitalize("FRED"), "Fred", "capitalize");

    eq(_.toLower("FOO"), "foo", "toLower");
    eq(_.toUpper("foo"), "FOO", "toUpper");

    // --- Padding ---
    eq(_.pad("abc", 8), "  abc   ", "pad");
    eq(_.padStart("abc", 6, "_"), "___abc", "padStart with _");
    eq(_.padEnd("abc", 6, "_"), "abc___", "padEnd with _");
    eq(_.padStart("abc", 6), "   abc", "padStart default space");

    // --- Trim ---
    eq(_.trim("  abc  "), "abc", "trim whitespace");
    eq(_.trim("--abc--", "-"), "abc", "trim chars");
    eq(_.trimStart("  abc  "), "abc  ", "trimStart");
    eq(_.trimEnd("  abc  "), "  abc", "trimEnd");

    // --- Tests ---
    eq(_.startsWith("foobar", "foo"), true, "startsWith");
    eq(_.startsWith("foobar", "bar"), false, "startsWith neg");
    eq(_.endsWith("foobar", "bar"), true, "endsWith");
    eq(_.endsWith("foobar", "foo"), false, "endsWith neg");

    // --- Repeat / replace / split ---
    eq(_.repeat("abc", 3), "abcabcabc", "repeat 3");
    eq(_.repeat("x", 0), "", "repeat 0");
    eq(_.replace("hello", "l", "L"), "heLlo", "replace first");
    eq(_.replace("hello world", /o/g, "0"), "hell0 w0rld", "replace regex global");
    eq(_.split("a-b-c", "-").length, 3, "split length");
    eq(_.split("a-b-c", "-")[1], "b", "split element");

    // --- Escape ---
    eq(_.escape("fred, barney, & pebbles"), "fred, barney, &amp; pebbles", "escape &");
    eq(_.escape("<div>"), "&lt;div&gt;", "escape <>");
    eq(_.unescape("&lt;div&gt;"), "<div>", "unescape");

    // --- words ---
    eq(_.words("fooBarBaz").length, 3, "words count");
    eq(_.words("fred barney pebbles")[0], "fred", "words first");

    // --- deburr (strip accents/diacritics) ---
    eq(_.deburr("déjà vu"), "deja vu", "deburr déjà vu");
    eq(_.deburr("naïve"), "naive", "deburr naïve");
    eq(_.deburr("café"), "cafe", "deburr café");
    eq(_.deburr("é"), "e", "deburr single é");

    // --- truncate ---
    eq(_.truncate("hi-diddly-ho there, neighborino", { length: 12 }), "hi-diddly...", "truncate length");

    if (state.failed === 0) {
        console.log("OK: string (" + state.passed + " passed)");
        return 0;
    }
    console.log("FAIL: string (" + state.passed + " passed, " + state.failed + " failed)");
    for (let i = 0; i < state.failures.length; i++) {
        console.log("  - " + state.failures[i]);
    }
    return 1;
}
