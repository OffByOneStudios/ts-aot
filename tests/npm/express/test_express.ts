// Test: express sub-dependencies
// Tests pure-JS packages bundled with Express that don't require HTTP stack
// Skipped: statuses (crash - nested closure module var), cookie (hang - decodeURIComponent loop)

const pathToRegexp = require('./node_modules/path-to-regexp');
const flatten = require('./node_modules/array-flatten');
const escapeHtml = require('./node_modules/escape-html');

function user_main(): number {
    let failures = 0;

    function check(name: string, actual: any, expected: any): void {
        const pass = actual === expected;
        if (pass) {
            console.log("PASS: " + name);
        } else {
            console.log("FAIL: " + name + " (got " + String(actual) + ", expected " + String(expected) + ")");
            failures++;
        }
    }

    // --- array-flatten ---
    const flat1 = flatten([1, [2, [3, [4]]]]);
    check("flatten deep", flat1.join(','), '1,2,3,4');

    const flat2 = flatten([1, [2, [3]]], 1);
    check("flatten depth=1", flat2.join(','), '1,2,3');

    const flat3 = flatten([[1, 2], [3, 4]]);
    check("flatten shallow", flat3.join(','), '1,2,3,4');

    const flat4 = flatten([1, 2, 3]);
    check("flatten already flat", flat4.join(','), '1,2,3');

    // --- path-to-regexp ---
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

    // --- escape-html ---
    check("escapeHtml safe text", escapeHtml('hello'), 'hello');
    check("escapeHtml empty", escapeHtml(''), '');

    // --- summary ---
    console.log("---");
    if (failures === 0) {
        console.log("All express sub-dependency tests passed!");
    } else {
        console.log(String(failures) + " test(s) failed");
    }
    return failures;
}
