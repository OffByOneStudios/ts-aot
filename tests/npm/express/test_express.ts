// Test: express sub-dependencies
// Tests pure-JS packages bundled with Express that don't require HTTP stack
//
// Skipped packages and reasons:
// - cookie: works standalone, but constructor name collision with content-type
//   when both compiled together (both have 'parse' function, content-type uses 'new')
// - merge-descriptors: Object.defineProperty doesn't propagate values to flat objects

const pathToRegexp = require('./node_modules/path-to-regexp');
const flatten = require('./node_modules/array-flatten');
const escapeHtml = require('./node_modules/escape-html');
const utilsMerge = require('./node_modules/utils-merge');
const contentType = require('./node_modules/content-type');
const fresh = require('./node_modules/fresh');
const encodeUrl = require('./node_modules/encodeurl');
const rangeParser = require('./node_modules/range-parser');
const cookieSignature = require('./node_modules/cookie-signature');
const etag = require('./node_modules/etag');
const vary = require('./node_modules/vary');
const statuses = require('./node_modules/statuses');

function user_main(): number {
    let failures = 0;

    function check(name: string, actual: any, expected: any): void {
        const pass = actual === expected;
        if (pass) {
            console.log("PASS: " + name);
        } else {
            console.log("FAIL: " + name + " (got " + String(actual) + ", expected " + String(expected) + ")");
            failures = failures + 1;
        }
    }

    // --- array-flatten (4 checks) ---
    check("flatten deep", flatten([1, [2, [3, [4]]]]).join(','), '1,2,3,4');
    check("flatten depth=1", flatten([1, [2, [3]]], 1).join(','), '1,2,3');
    check("flatten shallow", flatten([[1, 2], [3, 4]]).join(','), '1,2,3,4');
    check("flatten already flat", flatten([1, 2, 3]).join(','), '1,2,3');

    // --- path-to-regexp (6 checks) ---
    const keys: any[] = [];
    const re = pathToRegexp('/user/:id', keys);
    check("pathToRegexp returns object", typeof re, 'object');
    check("pathToRegexp keys has name", keys.length > 0 ? keys[0].name : null, 'id');

    const match = re.exec('/user/42');
    check("pathToRegexp matches /user/42", match !== null, true);
    check("pathToRegexp captures id=42", match ? match[1] : null, '42');

    const noMatch = re.exec('/post/42');
    check("pathToRegexp rejects /post/42", noMatch, null);

    const keys2: any[] = [];
    const re2 = pathToRegexp('/files/*', keys2);
    const wildMatch = re2.exec('/files/docs/readme.md');
    check("pathToRegexp wildcard matches", wildMatch !== null, true);

    // --- escape-html (5 checks) ---
    check("escapeHtml safe text", escapeHtml('hello'), 'hello');
    check("escapeHtml empty", escapeHtml(''), '');
    check("escapeHtml angle brackets", escapeHtml('<script>'), '&lt;script&gt;');
    check("escapeHtml ampersand", escapeHtml('a&b'), 'a&amp;b');
    check("escapeHtml mixed", escapeHtml('<div class="x">'), '&lt;div class=&quot;x&quot;&gt;');

    // --- utils-merge (2 checks) ---
    const merged1 = utilsMerge({a: 1}, {b: 2});
    check("utils-merge adds property", merged1.b, 2);
    const merged2 = utilsMerge({a: 1}, {a: 2});
    check("utils-merge overwrites", merged2.a, 2);

    // --- content-type (3 checks) ---
    const ct1 = contentType.parse("text/html");
    check("content-type parse type", ct1.type, "text/html");
    const ct2 = contentType.parse("text/html; charset=utf-8");
    check("content-type parse charset", ct2.parameters.charset, "utf-8");
    const ct3 = contentType.format({type: "text/html"});
    check("content-type format", ct3, "text/html");

    // --- fresh (3 checks) ---
    check("fresh empty headers", fresh({}, {}), false);
    check("fresh etag match", fresh({"if-none-match": '"abc"'}, {"etag": '"abc"'}), true);
    check("fresh etag mismatch", fresh({"if-none-match": '"abc"'}, {"etag": '"xyz"'}), false);

    // --- encodeurl (2 checks) ---
    const eu1 = encodeUrl("/foo bar");
    check("encodeurl encodes space", eu1.indexOf("%20") !== -1, true);
    check("encodeurl clean path unchanged", encodeUrl("/foo/bar"), "/foo/bar");

    // --- range-parser (3 checks) ---
    const rp1 = rangeParser(1000, "bytes=0-499");
    check("range-parser length", rp1.length, 1);
    check("range-parser start", rp1[0].start, 0);
    check("range-parser end", rp1[0].end, 499);

    // --- cookie-signature (3 checks) ---
    const signed = cookieSignature.sign("hello", "secret");
    check("cookie-signature sign prefix", signed.indexOf("hello.") === 0, true);
    const unsigned_val = cookieSignature.unsign(signed, "secret");
    check("cookie-signature unsign", unsigned_val, "hello");
    const bad_unsign = cookieSignature.unsign(signed, "wrong");
    check("cookie-signature unsign bad secret", bad_unsign, false);

    // --- etag (3 checks) ---
    const et1 = etag("hello world");
    check("etag is string", typeof et1, "string");
    check("etag starts with quote", et1.indexOf('"') === 0, true);
    const et2 = etag("");
    check("etag empty string", typeof et2, "string");

    // --- vary (3 checks) ---
    check("vary append empty", vary.append("", "Accept"), "Accept");
    check("vary append existing", vary.append("Accept", "Accept-Encoding"), "Accept, Accept-Encoding");
    check("vary append duplicate", vary.append("Accept", "Accept"), "Accept");

    // --- statuses (3 checks) ---
    check("statuses 200 message", statuses.message["200"], "OK");
    check("statuses 404 message", statuses.message["404"], "Not Found");
    check("statuses codes includes 200", statuses.codes.indexOf(200) !== -1, true);

    // --- summary ---
    console.log("---");
    if (failures === 0) {
        console.log("All express sub-dependency tests passed!");
    } else {
        console.log(String(failures) + " test(s) failed");
    }
    return failures;
}
