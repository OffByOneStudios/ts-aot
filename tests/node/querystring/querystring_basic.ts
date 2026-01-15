// Test querystring module basic functionality
import * as querystring from 'querystring';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: escape
    {
        const escaped = querystring.escape("hello world");
        if (escaped === "hello%20world") {
            console.log("PASS: escape space");
            passed++;
        } else {
            console.log("FAIL: escape space - got: " + escaped);
            failed++;
        }
    }

    // Test 2: escape special chars
    {
        const escaped = querystring.escape("a=b&c=d");
        if (escaped === "a%3Db%26c%3Dd") {
            console.log("PASS: escape special chars");
            passed++;
        } else {
            console.log("FAIL: escape special chars - got: " + escaped);
            failed++;
        }
    }

    // Test 3: unescape
    {
        const unescaped = querystring.unescape("hello%20world");
        if (unescaped === "hello world") {
            console.log("PASS: unescape");
            passed++;
        } else {
            console.log("FAIL: unescape - got: " + unescaped);
            failed++;
        }
    }

    // Test 4: unescape plus sign
    {
        const unescaped = querystring.unescape("hello+world");
        if (unescaped === "hello world") {
            console.log("PASS: unescape plus");
            passed++;
        } else {
            console.log("FAIL: unescape plus - got: " + unescaped);
            failed++;
        }
    }

    // Test 5: parse simple
    {
        const parsed = querystring.parse("foo=bar");
        if (parsed.foo === "bar") {
            console.log("PASS: parse simple");
            passed++;
        } else {
            console.log("FAIL: parse simple - got: " + parsed.foo);
            failed++;
        }
    }

    // Test 6: parse multiple
    {
        const parsed = querystring.parse("foo=bar&baz=qux");
        if (parsed.foo === "bar" && parsed.baz === "qux") {
            console.log("PASS: parse multiple");
            passed++;
        } else {
            console.log("FAIL: parse multiple");
            failed++;
        }
    }

    // Test 7: parse with encoding
    {
        const parsed = querystring.parse("name=hello%20world");
        if (parsed.name === "hello world") {
            console.log("PASS: parse with encoding");
            passed++;
        } else {
            console.log("FAIL: parse with encoding - got: " + parsed.name);
            failed++;
        }
    }

    // Test 8: stringify simple
    {
        const str = querystring.stringify({ foo: "bar" });
        if (str === "foo=bar") {
            console.log("PASS: stringify simple");
            passed++;
        } else {
            console.log("FAIL: stringify simple - got: " + str);
            failed++;
        }
    }

    // Test 9: stringify with space
    {
        const str = querystring.stringify({ name: "hello world" });
        if (str === "name=hello%20world") {
            console.log("PASS: stringify with space");
            passed++;
        } else {
            console.log("FAIL: stringify with space - got: " + str);
            failed++;
        }
    }

    // Test 10: decode alias (alias for parse)
    {
        const parsed = querystring.decode("foo=bar&baz=qux");
        if (parsed.foo === "bar" && parsed.baz === "qux") {
            console.log("PASS: decode alias");
            passed++;
        } else {
            console.log("FAIL: decode alias");
            failed++;
        }
    }

    // Test 11: encode alias (alias for stringify)
    {
        const str = querystring.encode({ test: "value" });
        if (str === "test=value") {
            console.log("PASS: encode alias");
            passed++;
        } else {
            console.log("FAIL: encode alias - got: " + str);
            failed++;
        }
    }

    console.log("");
    console.log("Results: " + passed + " passed, " + failed + " failed");

    return failed;
}
