// Debug: test if range.js internal variables are correct
const Range = require('./node_modules/semver/classes/range');

function user_main(): number {
    // Try constructing a Range - the issue is in parseRange's use of
    // comparatorTrimReplace which is destructured from require('../internal/re')

    // First, verify re module works when required directly
    const reModule = require('./node_modules/semver/internal/re');
    console.log("Direct access - comparatorTrimReplace: " + String(reModule.comparatorTrimReplace));

    // Now try to create a Range
    console.log("--- Creating Range ---");
    try {
        const r = new Range("1.0.0");
        console.log("Range 1.0.0: OK, set length=" + String(r.set.length));
    } catch (e: any) {
        console.log("Range 1.0.0 FAILED: " + e.message);
    }

    return 0;
}
